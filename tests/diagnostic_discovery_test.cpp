#include "DiagnosticDiscovery.h"
#include <assert.h>
#include <set>
#include <string>

int main() {
  std::set<std::string> ids, topics;
  for (unsigned i = 0; i < Diagnostics::Count; ++i) {
    JsonDocument config;
    DiagnosticDiscovery::build(config, i);
    std::string payload; serializeJson(config, payload);
    assert(payload.size() + 120 < 2048);
    assert(ids.insert(config["unique_id"].as<std::string>()).second);
    char topic[120]; DiagnosticDiscovery::configTopic(topic, sizeof(topic), i);
    assert(topics.insert(topic).second);
    assert(config["entity_category"] == "diagnostic");
    assert(config["command_topic"].isNull());
    DiagnosticDiscovery::topic(topic, sizeof(topic), i, "availability");
    assert(config["availability"][1]["topic"].as<std::string>() == topic);
    if (Diagnostics::fields[i].kind == Diagnostics::Text) {
      assert(config["state_class"].isNull());
      assert(config["unit_of_measurement"].isNull());
    }
  }
  for (bool alarm : {false, true}) {
    JsonDocument config; DiagnosticDiscovery::button(config, alarm);
    assert(config["payload_press"] == "PRESS");
    assert(config["retain"] == false);
    assert(config["entity_category"] == "diagnostic");
    assert(config["availability"].size() == (alarm ? 2 : 1));
    assert(ids.insert(config["unique_id"].as<std::string>()).second);
  }
  puts("Diagnostic discovery tests passed");
}
