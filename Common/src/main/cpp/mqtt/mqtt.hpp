/*
 * Minimal MQTT publisher for juggluco-server using libmosquitto.
 *
 * Publishes JSON payloads to a topic template with QoS 2 by default.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mqtt {

struct Config {
    bool enabled{false};

    std::string host{"localhost"};
    int port{1883};

    std::string username{};
    std::string password{};

    // If empty, a default will be generated: "juggluco-<deviceId>".
    std::string client_id{};

    // Topic template variables: {event} (and optionally {device} for multi-instance setups)
    std::string topic{"juggluco/{event}"};

    // 0..2 (defaults to 2)
    int qos{2};
    bool retain{false};
    int keepalive{60};

    // TLS (optional)
    std::string cafile{};
    std::string certfile{};
    std::string keyfile{};
    bool insecure{false};

    // Queueing
    std::size_t max_queue{1024};
};

namespace detail {
inline std::string boolerr(std::string_view key, std::string_view value) {
    return std::string(key) + " expects a boolean, got: '" + std::string(value) + "'";
}

inline bool parse_bool(std::string_view v, bool &out) {
    if(v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON" || v == "+") {
        out = true;
        return true;
    }
    if(v == "0" || v == "false" || v == "FALSE" || v == "off" || v == "OFF" || v == "-") {
        out = false;
        return true;
    }
    return false;
}

inline bool parse_int(std::string_view v, int &out) {
    if(v.empty()) return false;
    int sign = 1;
    std::size_t i = 0;
    if(v[0] == '+') { i = 1; }
    else if(v[0] == '-') { sign = -1; i = 1; }
    if(i >= v.size()) return false;
    long long acc = 0;
    for(; i < v.size(); ++i) {
        const char c = v[i];
        if(c < '0' || c > '9') return false;
        acc = acc * 10 + (c - '0');
        if(acc > 1'000'000'000LL) return false;
    }
    out = static_cast<int>(acc * sign);
    return true;
}
} // namespace detail

// Small helper for JSON string escaping.
inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for(unsigned char c : s) {
        switch(c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if(c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

// Apply one `key=value` from `.jugglucorc`. Returns false and sets `err` on invalid key/value.
inline bool apply_kv(Config &cfg, std::string_view key, std::string_view value, std::string &err) {
    if(key == "mqtt.enabled") {
        bool b;
        if(!detail::parse_bool(value, b)) { err = detail::boolerr(key, value); return false; }
        cfg.enabled = b;
        return true;
    }
    if(key == "mqtt.host") { cfg.host = std::string(value); return true; }
    if(key == "mqtt.port") {
        int p;
        if(!detail::parse_int(value, p) || p < 1 || p > 65535) { err = "mqtt.port expects 1..65535"; return false; }
        cfg.port = p;
        return true;
    }
    if(key == "mqtt.username") { cfg.username = std::string(value); return true; }
    if(key == "mqtt.password") { cfg.password = std::string(value); return true; }
    if(key == "mqtt.client_id") { cfg.client_id = std::string(value); return true; }
    if(key == "mqtt.topic") { cfg.topic = std::string(value); return true; }
    if(key == "mqtt.qos") {
        int q;
        if(!detail::parse_int(value, q) || q < 0 || q > 2) { err = "mqtt.qos expects 0..2"; return false; }
        cfg.qos = q;
        return true;
    }
    if(key == "mqtt.retain") {
        bool b;
        if(!detail::parse_bool(value, b)) { err = detail::boolerr(key, value); return false; }
        cfg.retain = b;
        return true;
    }
    if(key == "mqtt.keepalive") {
        int ka;
        if(!detail::parse_int(value, ka) || ka < 5 || ka > 3600) { err = "mqtt.keepalive expects 5..3600"; return false; }
        cfg.keepalive = ka;
        return true;
    }
    if(key == "mqtt.cafile") { cfg.cafile = std::string(value); return true; }
    if(key == "mqtt.certfile") { cfg.certfile = std::string(value); return true; }
    if(key == "mqtt.keyfile") { cfg.keyfile = std::string(value); return true; }
    if(key == "mqtt.insecure") {
        bool b;
        if(!detail::parse_bool(value, b)) { err = detail::boolerr(key, value); return false; }
        cfg.insecure = b;
        return true;
    }
    if(key == "mqtt.max_queue") {
        int mq;
        if(!detail::parse_int(value, mq) || mq < 1 || mq > 1000000) { err = "mqtt.max_queue expects 1..1000000"; return false; }
        cfg.max_queue = static_cast<std::size_t>(mq);
        return true;
    }
    err = "Unknown mqtt config key: " + std::string(key);
    return false;
}

// Initialize MQTT subsystem (no-op if cfg.enabled==false). Returns false on failure.
bool init(std::uint64_t device_id, const Config &cfg, std::string &err);

// Stop worker thread and release mosquitto resources.
void shutdown();

// Publish a JSON payload (must be a complete JSON object string).
void publish_json(std::string_view event, std::string_view json_object);

} // namespace mqtt
