/*
 * MQTT stub implementation for builds without libmosquitto.
 *
 * This must provide the same API as mqtt/mqtt.hpp without requiring
 * <mosquitto.h> or linking to libmosquitto.
 */

#include "mqtt/mqtt.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mqtt {

bool init(std::uint64_t /*device_id*/, const Config &cfg, std::string &err) {
    // If MQTT is disabled, behave like the real implementation: success.
    if(!cfg.enabled) {
        return true;
    }
    // If enabled but we built without MQTT support, fail clearly.
    err = "MQTT support not compiled in (reconfigure with -DJUGGLUCO_MQTT=ON and install libmosquitto dev package)";
    return false;
}

void shutdown() {
    // no-op
}

void publish_json(std::string_view /*event*/, std::string_view /*json_object*/) {
    // no-op
}

} // namespace mqtt
