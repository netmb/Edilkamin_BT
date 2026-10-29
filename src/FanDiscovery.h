#ifndef EDILKAMIN_FAN_DISCOVERY_H
#define EDILKAMIN_FAN_DISCOVERY_H

#include <ArduinoJson.h>
#include <stdio.h>
#include "FanControl.h"

namespace FanDiscovery {
inline bool climate(JsonDocument &config, const char *baseConfig,
                    const FanControl &fans) {
  if (deserializeJson(config, baseConfig))
    return false;
  config["optimistic"] = false;
  config["retain"] = false;
  config["min_temp"] = 5;
  config["max_temp"] = 35;
  if (!fans.hasFan(1)) {
    config.remove("fan_mode_command_topic");
    config.remove("fan_mode_state_topic");
    config.remove("fan_modes");
  }
  return true;
}

inline void extraFan(JsonDocument &config, uint8_t fan) {
  config.clear();
  char text[96];
  snprintf(text, sizeof(text), "Fan %u", fan);
  config["name"] = text;
  snprintf(text, sizeof(text), "322707E4_edilkamin_fan%u", fan);
  config["unique_id"] = text;
  config["icon"] = "mdi:fan";
  snprintf(text, sizeof(text), "edilkamin/322707E4/fan%u_mode/set", fan);
  config["command_topic"] = text;
  snprintf(text, sizeof(text), "edilkamin/322707E4/fan%u_mode/state", fan);
  config["state_topic"] = text;
  config["optimistic"] = false;
  config["retain"] = false; // HA commands must not replay on reconnect.
  JsonArray options = config["options"].to<JsonArray>();
  for (uint8_t level = 1; level <= 6; ++level)
    options.add(FanControl::mode(level));
  config["device"]["identifiers"].to<JsonArray>().add("322707E4");
  config["device"]["name"] = "Edilkamin";
  config["availability_mode"] = "all";
  JsonArray availability = config["availability"].to<JsonArray>();
  JsonObject bridge = availability.add<JsonObject>();
  bridge["topic"] = "edilkamin/322707E4/bridge/availability";
  bridge["payload_available"] = "ONLINE";
  bridge["payload_not_available"] = "OFFLINE";
  JsonObject control = availability.add<JsonObject>();
  snprintf(text, sizeof(text), "edilkamin/322707E4/fan%u_mode/availability", fan);
  control["topic"] = text;
  control["payload_available"] = "ONLINE";
  control["payload_not_available"] = "OFFLINE";
}
} // namespace FanDiscovery

#endif
