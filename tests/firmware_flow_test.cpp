#include "Helper.h"
#include "FanControl.h"
#include "StoveProtocol.h"
#include "SensorDiscovery.h"
#include "ModbusResponse.h"
#include <assert.h>
#include <map>
#include <string>
#include <vector>
#include <initializer_list>

struct String : std::string {
  using std::string::string;
  String(const std::string &value) : std::string(value) {}
};
#define F(x) x
#define debug(...)
#define debugln(...)
struct WiFiClient {};
struct PubSubClient {
  std::map<std::string, std::string> states;
  explicit PubSubClient(WiFiClient &) {}
  bool publish(const char *topic, const char *value, bool = false) {
    states[topic] = value;
    return true;
  }
};
struct Transport {
  std::vector<std::vector<byte>> writes;
  bool working = true;
  bool disconnected = false;
  bool writeValue(const byte *data, size_t) {
    if (!working) return false;
    writes.emplace_back(data, data + 6);
    return true;
  }
  void disconnect() { disconnected = true; }
} transport;
Transport *pRemoteCharacteristicWrite = &transport, *pClient = &transport;
Helper h;
bool doBtConnect = true, bleConnected = true;
unsigned long fakeMillis = 0;
unsigned long millis() { return fakeMillis; }
void hexDebug(byte *, size_t) {}
// Only encryption/physical transport is mocked; production protocol validation,
// MQTT callback, queue selection and response processing are compiled below.
void Helper::createBtPacket(byte *cmd, size_t len, byte *out) {
  memset(out, 0, 32);
  memcpy(out, cmd, len);
}
void Helper::aesDecrypt(byte *input, byte *output) { memcpy(output, input, 32); }
#include "firmware_under_test.inc"

static void reset() {
  for (auto &entry : btWriteQueue) entry.name = Helper::NO_CMD;
  controlTransaction.cancel();
  fanRefreshStep = fanRefreshComplete;
  fans = FanControl();
  fans.setEquipment(3);
  fans.setRelax(false);
  fans.updatePair(2, 3);
  fan1PowerValid = fan1PowerFresh = true;
  currentFan1Level = 6;
  currentPowerLevel = 2;
  automatic = false;
  mqttClient.states.clear();
  transport.writes.clear();
  transport.working = true;
  transport.disconnected = false;
  memset(sensorSeen, 0, sizeof(sensorSeen));
  memset(sensorValid, 0, sizeof(sensorValid));
}
static void mqtt(const char *topic, const std::string &value) {
  mqttCallback(String("edilkamin/322707E4/") + topic,
               reinterpret_cast<byte *>(const_cast<char *>(value.data())), value.size());
}
static void sent(std::initializer_list<byte> command) {
  assert(!transport.writes.empty());
  assert(transport.writes.back() == std::vector<byte>(command));
}
static bool reply(std::initializer_list<byte> frame, bool corrupt = false) {
  byte buffer[32] = {};
  assert(frame.size() + 22 <= sizeof(buffer));
  memcpy(buffer + 20, frame.begin(), frame.size());
  const auto crc = Modbus::crc16(buffer + 20, frame.size());
  buffer[20 + frame.size()] = (crc & 0xff) ^ (corrupt ? 1 : 0);
  buffer[21 + frame.size()] = crc >> 8;
  return processBtResponseData(buffer);
}
static std::string state(const char *suffix) {
  return mqttClient.states[std::string("edilkamin/322707E4/") + suffix];
}
int main() {
  reset();
  for (auto topic : {"hvac_mode/set", "automatic_mode/set", "relax/set",
                     "chrono_mode/set", "standby/set", "bluetooth/set",
                     "fan_mode/set", "preset_mode/set", "target_temperature/set"}) {
    mqtt(topic, "garbage");
    assert(!writeQueueHasElements());
    mqtt(topic, "");
    assert(!writeQueueHasElements());
  }
  mqtt("hvac_mode/set", std::string("heat\0off", 8));
  assert(!writeQueueHasElements());
  for (auto value : {"24", "24.0", "24.00"}) {
    mqtt("target_temperature/set", value);
    assert(writeBtData());
    sent({1, 6, 5, 37, 0, 240});
    assert(reply({1, 6, 5, 37, 0, 240}));
    assert(state("target_temperature/state") == "24.0");
  }
  mqtt("target_temperature/set", "24.50");
  assert(writeBtData()); sent({1, 6, 5, 37, 0, 245});
  assert(reply({1, 6, 5, 37, 0, 245}));
  mqtt("standby/set", "ON");
  assert(writeBtData()); sent({1, 6, 5, 44, 1, 0});
  assert(reply({1, 6, 5, 44, 1, 0}));
  assert(state("standby/state") == "ON");

  reset();
  mqtt("hvac_mode/set", "heat");
  mqtt("chrono_mode/set", "ON");
  assert(writeBtData()); sent({1, 3, 3, 28, 0, 1});
  mqtt("hvac_mode/set", "off"); // Must remain behind the older Chrono request.
  assert(!reply({1, 3, 2, 4, 0xa0}, true)); // No transition on a bad CRC.
  assert(controlTransaction.pending());
  assert(reply({1, 3, 2, 4, 0xa0}));
  fanRefreshStep = 0; // A fan refresh cannot interrupt a prepared shared write.
  assert(writeBtData()); sent({1, 6, 3, 28, 4, 0xa1});
  assert(reply({1, 6, 3, 28, 4, 0xa1}));
  assert(state("hvac_mode/state") == "heat");
  fanRefreshStep = fanRefreshComplete;
  assert(writeBtData()); sent({1, 3, 5, 34, 0, 1});
  assert(reply({1, 3, 2, 9, 0}));
  assert(writeBtData()); sent({1, 6, 5, 34, 9, 1});
  assert(reply({1, 6, 5, 34, 9, 1}));
  assert(writeBtData()); sent({1, 3, 3, 28, 0, 1});
  assert(reply({1, 3, 2, 0x55, 0xff}));
  assert(writeBtData()); sent({1, 6, 3, 28, 0x55, 0xfe});
  assert(reply({1, 6, 3, 28, 0x55, 0xfe}));
  assert(state("hvac_mode/state") == "off");
  assert(!writeQueueHasElements());

  reset();
  mqtt("chrono_mode/set", "OFF");
  assert(writeBtData());
  assert(reply({1, 0x83, 2})); // Unsupported register cancels the dependent write.
  assert(!controlTransaction.pending());
  assert(!writeBtData());
  mqtt("hvac_mode/set", "heat");
  assert(writeBtData());
  handleBtFailure(); // Timeout handler must cancel the same transaction.
  assert(!writeBtData());
  mqtt("hvac_mode/set", "heat");
  transport.working = false;
  assert(!writeBtData());
  assert(transport.disconnected && !controlTransaction.pending());

  reset();
  automatic = true;
  mqtt("preset_mode/set", "Man. P4");
  assert(writeBtData()); sent({1, 6, 5, 35, 0, 1});
  assert(!reply({1, 6, 5, 35, 1, 1})); // Wrong echo cannot unlock the power write.
  assert(reply({1, 6, 5, 35, 0, 1}));
  assert(!automatic);
  assert(writeBtData()); sent({1, 6, 5, 41, 6, 4});
  assert(reply({1, 6, 5, 41, 6, 4}));
  assert(state("preset_mode/state") == "Man. P4");
  mqtt("preset_mode/set", "Man. P1");
  assert(writeBtData());
  handleBtFailure();
  assert(!writeBtData()); // No power write after a missing Automatic-OFF echo.

  reset();
  queueBtCommand(Helper::READ_MAIN_ENV_TEMP, h.readMainEnvTemp);
  assert(writeBtData()); assert(reply({1, 3, 2, 1, 44}));
  assert(state("temperature/state") == "30.0");
  queueBtCommand(Helper::READ_MAIN_ENV_TEMP, h.readMainEnvTemp);
  assert(writeBtData()); assert(reply({1, 3, 2, 1, 0}));
  assert(state("temperature/state") == "25.6");
  queueBtCommand(Helper::READ_MAIN_ENV_TEMP, h.readMainEnvTemp);
  assert(writeBtData()); assert(reply({1, 3, 2, 0, 0}));
  assert(state("temperature/state") == "0.0");
  queueBtCommand(Helper::READ_FIREPLACE_COMMANDS, h.readFireplaceCommands);
  assert(writeBtData()); assert(reply({1, 3, 2, 4, 0x81}));
  assert(state("hvac_mode/state") == "heat");
  queueBtCommand(Helper::READ_ALARM, h.readAlarm);
  assert(writeBtData()); assert(reply({1, 3, 2, 12, 99}));
  assert(state("alarm_code/state") == "12");
  queueBtCommand(Helper::READ_WARNING_FLAGS, h.readWarningFlags);
  assert(writeBtData()); assert(reply({1, 3, 4, 0x10, 5, 0x80, 0x20}));
  assert(state("warning_flags/state") == "10058020");
  assert(state("pellet_reserve/state") == "ON");
  assert(state("pellet_level/state") == "Reserve");
  queueBtCommand(Helper::READ_WARNING_FLAGS, h.readWarningFlags);
  assert(writeBtData()); assert(reply({1, 3, 4, 0x10, 1, 0x80, 0x20}));
  assert(state("pellet_reserve/state") == "OFF");
  assert(state("pellet_level/state") == "Ok");
  queueBtCommand(Helper::READ_CANALIZED_2, h.readCanalized2);
  assert(writeBtData()); assert(reply({1, 3, 2, 1, 44}));
  assert(state("channel2_temperature/state") == "30.0");
  assert(sensorSeen[SensorDiscovery::Channel2] && sensorValid[SensorDiscovery::Channel2]);
  queueBtCommand(Helper::READ_CANALIZED_2, h.readCanalized2);
  assert(writeBtData()); assert(reply({1, 0x83, 2}));
  assert(state("channel2_temperature/availability") == "OFFLINE");
  assert(!sensorValid[SensorDiscovery::Channel2]);
  queueBtCommand(Helper::READ_CANALIZED_3, h.readCanalized3);
  assert(writeBtData()); assert(reply({1, 3, 2, 0, 215}));
  assert(state("channel3_temperature/state") == "21.5");
  queueBtCommand(Helper::READ_FIREPLACE_MAIN_STATUS, h.readFireplaceMainStatus);
  assert(writeBtData()); assert(reply({1, 3, 2, 0, 5}));
  assert(state("status/state") == "Final cleaning");
  assert(state("status_code/state") == "5/0");
  queueBtCommand(Helper::READ_FIREPLACE_MAIN_STATUS, h.readFireplaceMainStatus);
  assert(writeBtData()); assert(reply({1, 3, 2, 9, 123}));
  assert(state("status/state") == "Unknown (main=123, sub=9)");
  puts("Firmware MQTT / BLE flow tests passed (transport and AES mocked)");
}
