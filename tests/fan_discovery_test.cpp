#include "FanDiscovery.h"
#include "Helper.h"
#include <assert.h>
#include <string>

int main() {
  Helper helper;
  for (uint8_t code = 0; code <= 5; ++code) {
    FanControl fans;
    fans.setEquipment(code);
    JsonDocument config;
    assert(FanDiscovery::climate(config, helper.jsonAutodiscover, fans));
    const bool fan1 = code >= 1 && code <= 3;
    assert(!config["fan_modes"].isNull() == fan1);
    assert(!config["fan_mode_command_topic"].isNull() == fan1);
    assert(!config["fan_mode_state_topic"].isNull() == fan1);
    // Removing the fan must preserve climate identity and heating controls.
    assert(config["unique_id"] == "322707E4_edilkamin_climate");
    assert(config["temperature_command_topic"] == "edilkamin/322707E4/target_temperature/set");
    assert(config["preset_modes"].size() == 6);
    assert(config["min_temp"] == 5 && config["max_temp"] == 35);
    assert(config["optimistic"].is<bool>() && !config["optimistic"].as<bool>());
    assert(config["retain"].is<bool>() && !config["retain"].as<bool>());
    assert(config["device"]["identifiers"][0] == "322707E4");
  }
  for (uint8_t fan = 2; fan <= 3; ++fan) {
    JsonDocument config;
    FanDiscovery::extraFan(config, fan);
    std::string payload;
    serializeJson(config, payload);
    assert(payload.size() + 100 < 2048); // Includes MQTT discovery topic/headers.
    JsonDocument parsed;
    assert(!deserializeJson(parsed, payload));
    const std::string prefix = "edilkamin/322707E4/fan" + std::to_string(fan) + "_mode/";
    assert(parsed["command_topic"].as<std::string>() == prefix + "set");
    assert(parsed["state_topic"].as<std::string>() == prefix + "state");
    assert(parsed["unique_id"].as<std::string>() == "322707E4_edilkamin_fan" + std::to_string(fan));
    assert(parsed["name"].as<std::string>() == "Fan " + std::to_string(fan));
    assert(parsed["options"].size() == 6);
    assert(parsed["options"][0] == "20%");
    assert(parsed["options"][4] == "100%");
    assert(parsed["options"][5] == "Auto");
    assert(parsed["retain"].is<bool>() && !parsed["retain"].as<bool>());
    assert(parsed["optimistic"].is<bool>() && !parsed["optimistic"].as<bool>());
    assert(parsed["device"]["identifiers"][0] == "322707E4");
    assert(parsed["availability_mode"] == "all");
    assert(parsed["availability"][0]["topic"] == "edilkamin/322707E4/bridge/availability");
    assert(parsed["availability"][1]["topic"].as<std::string>() == prefix + "availability");
  }
  // A regression to the old Fan3/Relax address is immediately visible.
  assert(helper.setFan2[3] == 42 && helper.setFan3[3] == 42);
  assert(helper.setRelaxStatus[3] == 43);
  puts("MQTT discovery tests passed");
}
