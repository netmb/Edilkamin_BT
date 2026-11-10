#pragma once
#include "Diagnostics.h"
#include <ArduinoJson.h>
#include <stdio.h>

namespace DiagnosticDiscovery {
inline void topic(char *out, size_t size, unsigned id, const char *suffix) {
  snprintf(out, size, "edilkamin/322707E4/%s/%s", Diagnostics::fields[id].key, suffix);
}
inline void configTopic(char *out, size_t size, unsigned id) {
  snprintf(out, size, "homeassistant/sensor/edilkamin_322707E4_%s/config",
           Diagnostics::fields[id].key);
}
inline void availability(JsonDocument &config, const char *dataTopic = nullptr) {
  config["availability_mode"] = "all";
  JsonArray list = config["availability"].to<JsonArray>();
  JsonObject bridge = list.add<JsonObject>();
  bridge["topic"] = "edilkamin/322707E4/bridge/availability";
  bridge["payload_available"] = "ONLINE";
  bridge["payload_not_available"] = "OFFLINE";
  if (dataTopic) {
    JsonObject data = list.add<JsonObject>();
    data["topic"] = dataTopic;
    data["payload_available"] = "ONLINE";
    data["payload_not_available"] = "OFFLINE";
  }
}
inline void device(JsonDocument &config) {
  config["device"]["identifiers"].to<JsonArray>().add("322707E4");
  config["entity_category"] = "diagnostic";
}
inline void build(JsonDocument &config, unsigned id) {
  config.clear();
  const Diagnostics::Field &f = Diagnostics::fields[id];
  char buffer[120];
  config["name"] = f.name;
  snprintf(buffer, sizeof(buffer), "322707E4_edilkamin_%s", f.key);
  config["unique_id"] = buffer;
  topic(buffer, sizeof(buffer), id, "state");
  config["state_topic"] = buffer;
  device(config);
  if (f.unit) {
    config["unit_of_measurement"] = f.unit;
    config["device_class"] = "duration";
    config["state_class"] = "total_increasing";
  }
  topic(buffer, sizeof(buffer), id, "availability");
  availability(config, buffer);
}
inline void button(JsonDocument &config, bool alarm) {
  config.clear();
  config["name"] = alarm ? "Alarm quittieren" : "Diagnose aktualisieren";
  config["unique_id"] = alarm ? "322707E4_edilkamin_alarm_reset" : "322707E4_edilkamin_diagnostics_refresh";
  config["command_topic"] = alarm ? "edilkamin/322707E4/alarm_reset/set" : "edilkamin/322707E4/diagnostics/refresh";
  config["payload_press"] = "PRESS";
  config["retain"] = false;
  config["icon"] = alarm ? "mdi:alarm-check" : "mdi:refresh";
  device(config);
  availability(config, alarm ? "edilkamin/322707E4/alarm_reset/availability" : nullptr);
}
inline void result(JsonDocument &config) {
  config.clear();
  config["name"] = "Alarm acknowledgment status";
  config["unique_id"] = "322707E4_edilkamin_alarm_reset_result";
  config["state_topic"] = "edilkamin/322707E4/alarm_reset_result/state";
  device(config);
  availability(config);
}
} // namespace DiagnosticDiscovery
