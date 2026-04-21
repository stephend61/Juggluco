/*
 * Minimal MQTT publisher for juggluco-server using libmosquitto.
 */

#include "mqtt/mqtt.hpp"

#include <mosquitto.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace mqtt {
namespace {

struct Item {
    std::string event;
    std::string json;
};

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Item> g_q;

std::thread g_worker;
std::thread g_loop;
std::atomic<bool> g_stop{false};

// Guard lifetime/use of g_mosq + g_cfg between init/shutdown/publish/worker.
std::mutex g_state_mu;
std::condition_variable g_state_cv;
std::uint64_t g_generation{0};
std::uint32_t g_active_ops{0};
bool g_initialized{false};

std::uint64_t g_device_id{0};
Config g_cfg;

mosquitto *g_mosq{nullptr};
std::atomic<bool> g_connected{false};

struct StateGuard {
    mosquitto *mosq{nullptr};
    Config cfg{};
    std::uint64_t device_id{0};
    std::uint64_t gen{0};
    bool enabled{false};

    StateGuard() {
        std::unique_lock<std::mutex> lk(g_state_mu);
        gen = g_generation;
        // If shutdown() already started, treat as disabled.
        if(!g_initialized || g_stop.load(std::memory_order_acquire)) {
            return;
        }
        ++g_active_ops;
        mosq = g_mosq;
        cfg = g_cfg;
        device_id = g_device_id;
        enabled = cfg.enabled && mosq != nullptr;
    }
    ~StateGuard() {
        std::unique_lock<std::mutex> lk(g_state_mu);
        if(g_active_ops > 0) {
            --g_active_ops;
            if(g_active_ops == 0) {
                g_state_cv.notify_all();
            }
        }
    }
    StateGuard(const StateGuard &) = delete;
    StateGuard &operator=(const StateGuard &) = delete;
};

static std::string expand_topic(std::string_view topic_tmpl, std::uint64_t device_id, std::string_view event) {
    std::string topic(topic_tmpl);
    const std::string device = std::to_string(static_cast<unsigned long long>(device_id));

    auto replace_all = [&](std::string_view needle, const std::string &repl) {
        std::size_t pos = 0;
        while((pos = topic.find(needle.data(), pos, needle.size())) != std::string::npos) {
            topic.replace(pos, needle.size(), repl);
            pos += repl.size();
        }
    };

    replace_all("{device}", device);
    replace_all("{event}", std::string(event));
    return topic;
}

static void on_connect(mosquitto *, void *, int rc) {
    g_connected.store(rc == 0, std::memory_order_release);
}

static void on_disconnect(mosquitto *, void *, int) {
    g_connected.store(false, std::memory_order_release);
}

static void loop_main(mosquitto *mosq) {
    // Drive the mosquitto network loop in our own thread.
    // This avoids mosquitto_loop_start() failures on some builds/environments.
    while(!g_stop.load(std::memory_order_acquire)) {
        const int rc = mosquitto_loop(mosq, 200 /* ms */, 1 /* max packets */);
        if(rc == MOSQ_ERR_SUCCESS) {
            continue;
        }
        if(g_stop.load(std::memory_order_acquire)) {
            break;
        }
        if(rc == MOSQ_ERR_NO_CONN) {
            // If we were disconnected, ask mosquitto to reconnect asynchronously.
            (void)mosquitto_reconnect_async(mosq);
        }
        // Avoid busy looping on persistent errors.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

static void worker_main() {
    for(;;) {
        Item it;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return g_stop.load(std::memory_order_acquire) || !g_q.empty(); });
            if(g_stop.load(std::memory_order_acquire) && g_q.empty()) {
                return;
            }
            it = std::move(g_q.front());
            g_q.pop_front();
        }

        StateGuard st;
        if(!st.enabled) {
            continue;
        }

        const std::string topic = expand_topic(st.cfg.topic, st.device_id, it.event);

        const void *payload = it.json.data();
        const int payloadlen = static_cast<int>(it.json.size());
        const int qos = (st.cfg.qos < 0 ? 0 : (st.cfg.qos > 2 ? 2 : st.cfg.qos));
        const bool retain = st.cfg.retain;

        // Even if not connected yet, libmosquitto will queue a small amount internally.
        // We still keep our own bounded queue so we can drop if flooded.
        (void)mosquitto_publish(st.mosq, nullptr, topic.c_str(), payloadlen, payload, qos, retain);
    }
}

} // namespace

bool init(std::uint64_t device_id, const Config &cfg, std::string &err) {
    shutdown();

    if(!cfg.enabled) return true;

    if(cfg.host.empty()) {
        err = "mqtt.host is empty";
        return false;
    }
    if(cfg.qos < 0 || cfg.qos > 2) {
        err = "mqtt.qos must be 0..2";
        return false;
    }

    if(!cfg.username.empty() && cfg.password.empty()) {
        // Password is optional in MQTT generally; keep allowed.
    }

    // Make a local, validated copy for this init.
    Config local_cfg = cfg;
    if(local_cfg.client_id.empty()) {
        local_cfg.client_id = "juggluco-" + std::to_string(static_cast<unsigned long long>(device_id));
    }

    mosquitto_lib_init();
    mosquitto *mosq = mosquitto_new(local_cfg.client_id.c_str(), true /* clean session */, nullptr);
    if(!mosq) {
        err = "mosquitto_new() failed";
        mosquitto_lib_cleanup();
        return false;
    }

    mosquitto_threaded_set(mosq, true);
    mosquitto_connect_callback_set(mosq, &on_connect);
    mosquitto_disconnect_callback_set(mosq, &on_disconnect);

    if(!local_cfg.username.empty()) {
        if(mosquitto_username_pw_set(mosq, local_cfg.username.c_str(), local_cfg.password.empty() ? nullptr : local_cfg.password.c_str()) != MOSQ_ERR_SUCCESS) {
            err = "mosquitto_username_pw_set() failed";
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            return false;
        }
    }

    if(!local_cfg.cafile.empty() || !local_cfg.certfile.empty() || !local_cfg.keyfile.empty()) {
        const char *cafile = local_cfg.cafile.empty() ? nullptr : local_cfg.cafile.c_str();
        const char *certfile = local_cfg.certfile.empty() ? nullptr : local_cfg.certfile.c_str();
        const char *keyfile = local_cfg.keyfile.empty() ? nullptr : local_cfg.keyfile.c_str();
        if(mosquitto_tls_set(mosq, cafile, nullptr, certfile, keyfile, nullptr) != MOSQ_ERR_SUCCESS) {
            err = "mosquitto_tls_set() failed";
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            return false;
        }
        mosquitto_tls_insecure_set(mosq, local_cfg.insecure);
    }

    mosquitto_reconnect_delay_set(mosq, 1, 30, true);
    {
        const int rc = mosquitto_connect_async(mosq, local_cfg.host.c_str(), local_cfg.port, local_cfg.keepalive);
        if(rc != MOSQ_ERR_SUCCESS) {
            err = std::string("mosquitto_connect_async() failed: ") + mosquitto_strerror(rc);
            if(rc == MOSQ_ERR_ERRNO) {
                err += std::string(" errno=") + std::to_string(errno) + " (" + std::strerror(errno) + ")";
            }
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            return false;
        }
    }

    // Ensure threads see "not stopping" before they start.
    g_stop.store(false, std::memory_order_release);

    try {
        g_loop = std::thread(&loop_main, mosq);
    } catch(...) {
        err = "Failed to start mosquitto loop thread";
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return false;
    }

    {
        // Commit global state only after mosquitto is fully configured and the loop is running.
        std::lock_guard<std::mutex> lk(g_state_mu);
        ++g_generation;
        g_device_id = device_id;
        g_cfg = local_cfg;
        g_mosq = mosq;
        g_connected.store(false, std::memory_order_release);
        g_stop.store(false, std::memory_order_release);
        g_initialized = true;
    }

    try {
        g_worker = std::thread(&worker_main);
    } catch(...) {
        // Roll back to a clean state.
        {
            std::lock_guard<std::mutex> lk(g_state_mu);
            g_stop.store(true, std::memory_order_release);
            ++g_generation;
            g_mosq = nullptr;
            g_initialized = false;
        }
        if(g_loop.joinable()) {
            g_loop.join();
        }
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        err = "Failed to start MQTT worker thread";
        return false;
    }
    return true;
}

void shutdown() {
    mosquitto *mosq = nullptr;
    {
        std::unique_lock<std::mutex> lk(g_state_mu);
        if(!g_initialized && g_mosq == nullptr) {
            // Nothing to do.
        }
        // Block new publish/worker mosq use.
        g_stop.store(true, std::memory_order_release);
        ++g_generation;
        // Wait for any active operations to finish.
        g_state_cv.wait(lk, [] { return g_active_ops == 0; });
        mosq = g_mosq;
        g_mosq = nullptr;
        g_initialized = false;
    }

    // Stop worker & drop queued items. Shutdown should not block waiting for flush.
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_q.clear();
    }
    g_cv.notify_all();
    if(g_worker.joinable()) {
        g_worker.join();
    }

    if(g_loop.joinable()) {
        g_loop.join();
    }

    if(mosq) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }
    g_connected.store(false, std::memory_order_release);
}

void publish_json(std::string_view event, std::string_view json_object) {
    StateGuard st;
    if(!st.enabled) return;

    // Ensure it looks like an object (avoid accidental publishing of garbage).
    // Allow leading whitespace.
    const std::size_t first = json_object.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos || json_object[first] != '{') return;

    Item it{std::string(event), std::string(json_object)};
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if(g_q.size() >= st.cfg.max_queue) {
            g_q.pop_front();
        }
        g_q.push_back(std::move(it));
    }
    g_cv.notify_one();
}

} // namespace mqtt

