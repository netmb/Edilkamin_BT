#include "SensorDiscovery.h"
#include <assert.h>
#include <string>
#include <set>
int main() {
  std::set<std::string> topics, ids;
  for (unsigned i = 0; i < SensorDiscovery::Count; ++i) {
    auto id = static_cast<SensorDiscovery::Id>(i);
    JsonDocument config;
    SensorDiscovery::build(config, id);
    std::string payload;
    serializeJson(config, payload);
    assert(payload.size() + 110 < 2048);
    JsonDocument parsed;
    assert(!deserializeJson(parsed, payload));
    char topic[100];
    SensorDiscovery::configTopic(topic, sizeof(topic), id);
    assert(topics.insert(topic).second);
    assert(ids.insert(parsed["unique_id"].as<std::string>()).second);
    SensorDiscovery::topic(topic, sizeof(topic), id, "state");
    assert(parsed["state_topic"].as<std::string>() == topic);
    assert(parsed["command_topic"].isNull());
    assert(parsed["availability_mode"] == "all");
    assert(parsed["availability"][0]["topic"] == "edilkamin/322707E4/bridge/availability");
    SensorDiscovery::topic(topic, sizeof(topic), id, "availability");
    assert(parsed["availability"][1]["topic"].as<std::string>() == topic);
    assert(parsed["device"]["identifiers"][0] == "322707E4");
    if (id == SensorDiscovery::Channel2 || id == SensorDiscovery::Channel3) {
      assert(parsed["device_class"] == "temperature");
      assert(parsed["unit_of_measurement"] == "°C");
      assert(parsed["state_class"] == "measurement");
    } else {
      assert(parsed["unit_of_measurement"].isNull());
      assert(parsed["entity_category"] == "diagnostic");
    }
    if (id == SensorDiscovery::PelletReserve) {
      assert(parsed["payload_on"] == "ON" && parsed["payload_off"] == "OFF");
    }
  }
  puts("Sensor discovery tests passed");
}
