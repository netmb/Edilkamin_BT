#ifndef EDILKAMIN_SENSOR_DISCOVERY_H
#define EDILKAMIN_SENSOR_DISCOVERY_H
#include <ArduinoJson.h>
#include <stdio.h>

namespace SensorDiscovery {
enum Id { Alarm, Warnings, PelletReserve, Channel2, Channel3, StatusCode, Count };
struct Sensor { const char *key; const char *name; bool binary; bool temperature; };
static const Sensor sensors[] = {
    {"alarm_code", "Alarm code", false, false},
    {"warning_flags", "Warning flags (raw)", false, false},
    {"pellet_reserve", "Pellet reserve", true, false},
    {"channel2_temperature", "Channel 2 temperature", false, true},
    {"channel3_temperature", "Channel 3 temperature", false, true},
    {"status_code", "Status code (raw)", false, false}};
inline void topic(char *out, size_t size, Id id, const char *suffix) {
  snprintf(out, size, "edilkamin/322707E4/%s/%s", sensors[id].key, suffix);
}
inline void configTopic(char *out, size_t size, Id id) {
  snprintf(out, size, "homeassistant/%s/edilkamin_322707E4_%s/config",
           sensors[id].binary ? "binary_sensor" : "sensor", sensors[id].key);
}
inline void build(JsonDocument &config, Id id) {
  config.clear();
  const Sensor &sensor = sensors[id];
  char buffer[100];
  config["name"] = sensor.name;
  snprintf(buffer, sizeof(buffer), "322707E4_edilkamin_%s", sensor.key);
  config["unique_id"] = buffer;
  topic(buffer, sizeof(buffer), id, "state");
  config["state_topic"] = buffer;
  config["device"]["identifiers"].to<JsonArray>().add("322707E4");
  if (sensor.temperature) {
    config["device_class"] = "temperature";
    config["unit_of_measurement"] = "°C";
    config["state_class"] = "measurement";
  } else {
    config["entity_category"] = "diagnostic";
  }
  if (sensor.binary) {
    config["payload_on"] = "ON";
    config["payload_off"] = "OFF";
    config["device_class"] = "problem";
  }
  config["availability_mode"] = "all";
  JsonArray availability = config["availability"].to<JsonArray>();
  JsonObject bridge = availability.add<JsonObject>();
  bridge["topic"] = "edilkamin/322707E4/bridge/availability";
  bridge["payload_available"] = "ONLINE";
  bridge["payload_not_available"] = "OFFLINE";
  JsonObject data = availability.add<JsonObject>();
  topic(buffer, sizeof(buffer), id, "availability");
  data["topic"] = buffer;
  data["payload_available"] = "ONLINE";
  data["payload_not_available"] = "OFFLINE";
}
} // namespace SensorDiscovery
#endif
