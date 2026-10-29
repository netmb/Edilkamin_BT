#include <Arduino.h>
#include <ArduinoJson.h>
#include <Helper.h>
#include "FanControl.h"
#include "FanDiscovery.h"
#include "ModbusResponse.h"
#include "StoveProtocol.h"
#include "SensorDiscovery.h"
#include "FirmwareVersion.h"
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <TelnetStream.h>
#include <WiFi.h>
#include <WiFiManager.h>

#define DEBUG 1 // 0 = Off, 1 = Local, 2 = Telnet
#if DEBUG == 1
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#elif DEBUG == 2
#define debug(x) TelnetStream.print(x)
#define debugln(x) TelnetStream.println(x)
#else
#define debug(x)
#define debugln(x)
#endif

const char *hostname PROGMEM = "edilkaminble";

char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[32];
char mqtt_pass[32];
char ntp_server[40] = "pool.ntp.org";
char ntp_offset[6] = "7200";

// Flag for saving data
bool shouldSaveConfig = false;

// Callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

enum states {
  START,
  BT_CONNECT_CHECK,
  BT_CHECK_ON_OFF_TIMES,
  CHECK_BT_WRITE_QUEUE,
  DO_NEXT_QUERY,
  BT_WRITE_REQUEST,
  AWAIT_BT_RESPONSE,
  PROCESSING_BT_RESPONSE,
  DONE_PROCESSING_BT_RESPONSE,
};

Helper h;
uint8_t status = START;
bool doBtConnect = true;
bool btResponse = false;
byte *btData;
bool btWriteRequest = false;
static BLEUUID serviceUUID("abf0");
static BLEUUID charUUIDWrite("abf1");
static BLEUUID charUUIDRead("abf2");
static boolean bleDoConnect = false;
static boolean bleConnected = false;
static boolean bleDoScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristicWrite;
static BLERemoteCharacteristic *pRemoteCharacteristicRead;
static BLEAdvertisedDevice *myDevice;
static BLEClient *pClient;
unsigned long currentMillis = 0;
unsigned long msLastQuery = 0;
unsigned long msLastMqttConnect = 0;
unsigned long msLastBleConnect = 0;
unsigned long writeTimestamp = 0;
unsigned long bleLastConnect = 0;
unsigned long bleLastSwitchOff = 0;
const long queryInterval = 2000;
const long responseTimeout = 2000;
const long connectTimeout = 1000 * 30;     // 30 sec
const long bleConnectTimeout = 1000 * 300; // 5 min
const long bleOnTime = 1000 * 30;          // 30 sec
const long bleOffTime = 1000 * 120;        // 2 min

FanControl fans;
bool fanDiscoveryDirty = true;
bool fan1PowerValid = false;
bool fan1PowerFresh = false;
// Bootstrap reads run once per connection, independently of the rotating poll.
const uint8_t fanRefreshComplete = 4;
uint8_t fanRefreshStep = fanRefreshComplete;
volatile bool bleDisconnected = false;
byte currentFan1Level = 0;
byte currentPowerLevel = 0;
bool automatic = false;
StoveProtocol::ControlTransaction controlTransaction;
Helper::btCmds transactionOp = Helper::NO_CMD;
bool sensorDiscoveryDirty = true;
bool sensorSeen[SensorDiscovery::Count] = {};
bool sensorValid[SensorDiscovery::Count] = {};
char sensorValues[SensorDiscovery::Count][32] = {};
unsigned long sensorUpdated[SensorDiscovery::Count] = {};
// Polls span several BLE windows; stale data must eventually become unavailable.
const unsigned long sensorMaxAge = 10UL * 60 * 1000;

struct btWriteMapping {
  Helper::btCmds name;
  byte cmd[6];
};

uint8_t btWriteQueueIndex = 0;
const uint8_t btWriteQueueLength = 10;
btWriteMapping btWriteQueue[btWriteQueueLength] = {};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Helper::btCmds currentOp;

struct queryLookup {
  const Helper::btCmds name;
  const byte *cmd;
};
uint8_t queryIndex = 0;
const queryLookup queryQueue[] = {
    {Helper::READ_FIREPLACE_COMMANDS, h.readFireplaceCommands},
    {Helper::READ_ALARM, h.readAlarm},
    {Helper::READ_CANALIZED_2, h.readCanalized2},
    {Helper::READ_CANALIZED_3, h.readCanalized3},
    {Helper::READ_POWER, h.readPower},
    {Helper::READ_AUTOMATIC, h.readAutomatic},
    {Helper::READ_ON_OFF_CHRONO, h.readOnOffChrono},
    {Helper::READ_MAIN_ENV_TEMP, h.readMainEnvTemp},
    {Helper::READ_THERMOCOUPLE_TEMP, h.readThermocoupleTemperature},
    {Helper::READ_TEMPERATURE, h.readTemperature},
    {Helper::READ_FAN, h.readFan},
    {Helper::READ_NUMBER_FAN, h.readNumberFan},
    {Helper::READ_FAN_23, h.readFan23},
    {Helper::READ_RELAX, h.readRelax},
    {Helper::READ_FIREPLACE_MAIN_STATUS, h.readFireplaceMainStatus},
    {Helper::READ_STANDBY_STATUS, h.readStandbyStatus},
    {Helper::READ_WARNING_FLAGS, h.readWarningFlags}};
const size_t queryQueueElements = sizeof(queryQueue) / sizeof(queryQueue[0]);

void publishFanAvailability() {
  for (uint8_t fan = 2; fan <= 3; ++fan) {
    char topic[80];
    snprintf(topic, sizeof(topic), "edilkamin/322707E4/fan%u_mode/availability", fan);
    mqttClient.publish(topic, fans.canAccept(fan) ? "ONLINE" : "OFFLINE", true);
  }
}

void publishExtraFanStates() {
  for (uint8_t fan = 2; fan <= 3; ++fan) {
    if (!fans.hasFan(fan))
      continue;
    char topic[80];
    snprintf(topic, sizeof(topic), "edilkamin/322707E4/fan%u_mode/state", fan);
    const byte level = fan == 2 ? fans.fan2() : fans.fan3();
    mqttClient.publish(topic, fans.pairValid() ? FanControl::mode(level) : "None",
                       true);
  }
  publishFanAvailability();
}

void publishFanDiscovery() {
  if (!mqttClient.connected())
    return;
  JsonDocument climate;
  if (!FanDiscovery::climate(climate, h.jsonAutodiscover, fans))
    return;
  String payload;
  serializeJson(climate, payload);
  bool success = mqttClient.publish(
      "homeassistant/climate/edilkamin_322707E4/config", payload.c_str(), true);
  for (uint8_t fan = 2; fan <= 3; ++fan) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic),
             "homeassistant/select/edilkamin_322707E4_fan%u/config", fan);
    // Do not delete retained entities merely because detection is not ready.
    if (!fans.equipmentKnown())
      continue;
    if (!fans.hasFan(fan)) {
      success = mqttClient.publish(configTopic, "", true) && success;
      char stateTopic[80];
      snprintf(stateTopic, sizeof(stateTopic),
               "edilkamin/322707E4/fan%u_mode/state", fan);
      success = mqttClient.publish(stateTopic, "", true) && success;
      continue;
    }
    JsonDocument config;
    FanDiscovery::extraFan(config, fan);
    payload = "";
    serializeJson(config, payload);
    success = mqttClient.publish(configTopic, payload.c_str(), true) && success;
  }
  fanDiscoveryDirty = !success;
  publishExtraFanStates();
}

void publishSensor(SensorDiscovery::Id id, const char *value) {
  if (!sensorSeen[id]) sensorDiscoveryDirty = true;
  sensorSeen[id] = sensorValid[id] = true;
  sensorUpdated[id] = millis();
  snprintf(sensorValues[id], sizeof(sensorValues[id]), "%s", value);
  char topic[100];
  SensorDiscovery::topic(topic, sizeof(topic), id, "state");
  const bool sent = mqttClient.publish(topic, value, true);
  SensorDiscovery::topic(topic, sizeof(topic), id, "availability");
  if (!mqttClient.publish(topic, sent ? "ONLINE" : "OFFLINE", true) || !sent)
    sensorDiscoveryDirty = true;
}

void invalidateSensor(SensorDiscovery::Id id) {
  sensorValid[id] = false;
  char topic[100];
  SensorDiscovery::topic(topic, sizeof(topic), id, "availability");
  if (!mqttClient.publish(topic, "OFFLINE", true)) sensorDiscoveryDirty = true;
}

void publishSensorDiscovery() {
  bool success = true;
  for (unsigned i = 0; i < SensorDiscovery::Count; ++i) {
    const auto id = static_cast<SensorDiscovery::Id>(i);
    char topic[100];
    // Never announce a temperature on a stove that has not returned this sensor.
    if (sensorSeen[i]) {
      JsonDocument config;
      SensorDiscovery::build(config, id);
      String payload;
      serializeJson(config, payload);
      SensorDiscovery::configTopic(topic, sizeof(topic), id);
      success = mqttClient.publish(topic, payload.c_str(), true) && success;
      SensorDiscovery::topic(topic, sizeof(topic), id, "state");
      success = mqttClient.publish(topic, sensorValues[i], true) && success;
    }
    SensorDiscovery::topic(topic, sizeof(topic), id, "availability");
    success = mqttClient.publish(topic, sensorValid[i] && success ? "ONLINE" : "OFFLINE", true) && success;
  }
  sensorDiscoveryDirty = !success;
}

void publishTemperature(const char *topic, const byte *data) {
  char value[12];
  StoveProtocol::temperature(StoveProtocol::word(data), value, sizeof(value));
  mqttClient.publish(topic, value);
}

void hexDebug(byte *data, size_t length) {
  char output[(length * 2) + 1];
  char *ptr = &output[0];
  int i;
  for (i = 0; i < length; i++) {
    ptr += sprintf(ptr, "%02X", (int)data[i]);
  }
  debug(output);
}

bool queueBtCommand(Helper::btCmds name, const byte *cmd) {
  for (uint8_t i = 0; i < btWriteQueueLength; i++) {
    if (btWriteQueue[i].name == Helper::NO_CMD) {
      btWriteQueue[i].name = name;
      memcpy(btWriteQueue[i].cmd, cmd, 6);
      debug(F(", queued at index:"));
      debug(i);
      debugln();
      doBtConnect = true;
      return true;
    }
  }
  debugln(F("Command queue full: command rejected"));
  return false;
}

void nextQuery() {
  // Candidate channels follow the detected equipment; a valid sensor reply
  // is additionally required before discovery/state publication.
  while ((queryQueue[queryIndex].name == Helper::READ_FAN_23 && !fans.hasExtraFans()) ||
         (queryQueue[queryIndex].name == Helper::READ_CANALIZED_2 && !fans.hasFan(2)) ||
         (queryQueue[queryIndex].name == Helper::READ_CANALIZED_3 && !fans.hasFan(3)))
    queryIndex = (queryIndex + 1) % queryQueueElements;
  byte btCmd[6];
  debugln();
  debug(F("Next Query Command:"));
  debug(queryQueue[queryIndex].name);
  debug(F(",command:"));
  hexDebug((unsigned char *)queryQueue[queryIndex].cmd, 6);
  memcpy(btCmd, queryQueue[queryIndex].cmd, 6);
  queueBtCommand(queryQueue[queryIndex].name, btCmd);

  if (queryIndex < queryQueueElements - 1)
    queryIndex++;
  else
    queryIndex = 0;
}

bool writeQueueHasElements() {
  if (fanRefreshStep < fanRefreshComplete || fans.pending() || controlTransaction.pending())
    return true;
  for (uint8_t i = 0; i < btWriteQueueLength; i++) {
    if (btWriteQueue[i].name != Helper::NO_CMD)
      return true;
  }
  return false;
}

static byte lastBtPacket[32];
static byte lastBtCommand[6];

void handleBtFailure() {
  controlTransaction.cancel();
  if (currentOp == Helper::READ_ALARM) invalidateSensor(SensorDiscovery::Alarm);
  if (currentOp == Helper::READ_WARNING_FLAGS) {
    invalidateSensor(SensorDiscovery::Warnings);
    invalidateSensor(SensorDiscovery::PelletReserve);
  }
  if (currentOp == Helper::READ_CANALIZED_2) invalidateSensor(SensorDiscovery::Channel2);
  if (currentOp == Helper::READ_CANALIZED_3) invalidateSensor(SensorDiscovery::Channel3);
  if (currentOp == Helper::READ_FIREPLACE_MAIN_STATUS) invalidateSensor(SensorDiscovery::StatusCode);
  if (currentOp == Helper::READ_FAN || currentOp == Helper::READ_POWER ||
      currentOp == Helper::SET_FAN_1 || currentOp == Helper::SET_WRITE_NEW_POWER)
    fan1PowerFresh = false;
  if (currentOp == Helper::SET_FAN_2 || currentOp == Helper::SET_FAN_3 ||
      currentOp == Helper::READ_FAN_23) {
    fans.failWrite();
    publishExtraFanStates();
  }
  if (currentOp == Helper::READ_NUMBER_FAN) {
    fans.setEquipment(FanControl::UnknownEquipment);
    fanDiscoveryDirty = true;
    publishFanAvailability();
  }
  if (currentOp == Helper::READ_RELAX || currentOp == Helper::SET_RELAX_STATUS) {
    fans.cancelPending();
    fans.requireRefresh();
  }
}

bool writeBtData() {
  // Finish a confirmed prerequisite before any bootstrap or queued operation.
  bool selected = controlTransaction.take(lastBtCommand);
  if (selected) currentOp = transactionOp;
  while (fanRefreshStep < fanRefreshComplete && !selected) {
    const uint8_t step = fanRefreshStep++;
    if (step == 0) {
      currentOp = Helper::READ_NUMBER_FAN;
      memcpy(lastBtCommand, h.readNumberFan, 6);
    } else if (step == 1) {
      currentOp = Helper::READ_RELAX;
      memcpy(lastBtCommand, h.readRelax, 6);
    } else if (step == 2) {
      currentOp = Helper::READ_FAN;
      memcpy(lastBtCommand, h.readFan, 6);
    } else if (fans.hasExtraFans()) {
      currentOp = Helper::READ_FAN_23;
      memcpy(lastBtCommand, h.readFan23, 6);
    } else {
      continue;
    }
    selected = true;
  }
  for (int i = 0; i < btWriteQueueLength && !selected; ++i) {
    if (btWriteQueue[i].name == Helper::NO_CMD)
      continue;
    currentOp = btWriteQueue[i].name;
    memcpy(lastBtCommand, btWriteQueue[i].cmd, 6);
    // Keep arrival order even when new MQTT commands arrive during a request.
    for (uint8_t j = i; j + 1 < btWriteQueueLength; ++j)
      btWriteQueue[j] = btWriteQueue[j + 1];
    btWriteQueue[btWriteQueueLength - 1].name = Helper::NO_CMD;
    --i; // The next candidate moved into this slot if validation rejects it.
    if (currentOp == Helper::SET_FAN_1) {
      if (!fan1PowerFresh || !fans.settingsFresh() || !fans.canAccept(1))
        continue;
      lastBtCommand[5] = currentPowerLevel;
    } else if (currentOp == Helper::SET_WRITE_NEW_POWER) {
      if (!fan1PowerFresh)
        continue;
      lastBtCommand[4] = currentFan1Level;
    }
    if (currentOp == Helper::SET_ON_OFF || currentOp == Helper::SET_ON_OFF_CHRONO ||
        currentOp == Helper::SET_WRITE_NEW_POWER) {
      transactionOp = currentOp;
      controlTransaction.begin(lastBtCommand);
      currentOp = transactionOp == Helper::SET_ON_OFF ? Helper::READ_FIREPLACE_COMMANDS :
                  transactionOp == Helper::SET_ON_OFF_CHRONO ? Helper::READ_ON_OFF_CHRONO :
                  Helper::SET_CHANGE_AUTO_SWITCH;
    }
    selected = true;
  }
  if (!selected && fans.pending()) {
    selected = fans.beginWrite(lastBtCommand);
    currentOp = Helper::SET_FAN_2; // Both values use the same register/echo.
    if (!selected) {
      fans.cancelPending();
      debugln(F("Fan command cancelled: fresh settings unavailable or Relax active"));
    }
  }
  if (!selected)
    return false;
  debug(F("-> Write cmd:"));
  debug(currentOp);
  debug(F(", data:"));
  hexDebug(lastBtCommand, 6);
  debugln();
  h.createBtPacket(lastBtCommand, 6, lastBtPacket);
  if (!pRemoteCharacteristicWrite->writeValue(lastBtPacket, 32)) {
    handleBtFailure();
    pClient->disconnect();
    return false;
  }
  return true;
}
bool processBtResponseData(byte *btData) {
  debug(F("<- Read, data:"));
  Helper::structDatagram d;
  byte decrypted[32];
  h.aesDecrypt(btData, decrypted);
  const Modbus::Response response =
      Modbus::validate(decrypted + 20, 12, lastBtCommand);
  if (response == Modbus::Response::Invalid) {
    debugln(F("Ignoring invalid/mismatched Modbus response"));
    return false;
  }
  if (response == Modbus::Response::Exception) {
    debug(F("Modbus exception: "));
    debugln(decrypted[22]);
    handleBtFailure();
    return true;
  }
  controlTransaction.acknowledge(decrypted + 20);
  memcpy(&d, decrypted, sizeof(d));
  hexDebug(d.payload, 6);
  debugln();
  if (d.payload[1] == 6) { // Set Response
    debug(F("Set-Response for cmd:"));
    debug(currentOp);
    debug("; data:");
    hexDebug(d.payload, 6);
    debugln("");
    if (currentOp == Helper::SET_FAN_2 || currentOp == Helper::SET_FAN_3) {
      fans.updatePair(d.payload[4], d.payload[5]);
      publishExtraFanStates();
    } else if (currentOp == Helper::SET_ON_OFF) {
      mqttClient.publish("edilkamin/322707E4/hvac_mode/state",
                         (d.payload[5] & 1) ? "heat" : "off");
    } else if (currentOp == Helper::SET_RELAX_STATUS) {
      fans.setRelax(d.payload[4] != 0 || d.payload[5] != 0);
      publishFanAvailability();
      if (d.payload[4] == 0 && d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/relax/state", "OFF");
      else if (d.payload[4] == 1 && d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/relax/state", "ON");
    } else if (currentOp == Helper::SET_WRITE_NEW_POWER && !automatic) {
      currentFan1Level = d.payload[4];
      currentPowerLevel = d.payload[5];
      switch (d.payload[5]) {
      case 1:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P1");
        break;
      case 2:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P2");
        break;
      case 3:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P3");
        break;
      case 4:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P4");
        break;
      case 5:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P5");
        break;
      }
    } else if (currentOp == Helper::SET_FAN_1) {
      currentFan1Level = d.payload[4];
      currentPowerLevel = d.payload[5];
      switch (d.payload[4]) {
      case 1:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "20%");
        break;
      case 2:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "40%");
        break;
      case 3:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "60%");
        break;
      case 4:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "80%");
        break;
      case 5:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "100%");
        break;
      case 6:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "Auto");
        break;
      }
    } else if (currentOp == Helper::SET_ON_OFF_CHRONO) {
      if (d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "OFF");
      else if (d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "ON");
    } else if (currentOp == Helper::SET_STANDBY_STATUS) {
      if (d.payload[4] == 0 && d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/standby/state", "OFF");
      else if (d.payload[4] == 1 && d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/standby/state", "ON");
    } else if (currentOp == Helper::SET_CHANGE_AUTO_SWITCH) {
      if (d.payload[4] == 0) {
        automatic = false;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "OFF");
      } else if (d.payload[4] == 1) {
        automatic = true;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "ON");
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Auto");
      }
    } else if (currentOp == Helper::SET_WRITE_NEW_TEMP) {
      publishTemperature("edilkamin/322707E4/target_temperature/state", d.payload + 4);
    }
  } else if (d.payload[1] == 3) { // Query Response
    debug(F("Response for query-cmd:"));
    debug(currentOp);
    debug("; data:");
    hexDebug(d.payload, 6);

    debugln();
    if (currentOp == Helper::READ_FIREPLACE_COMMANDS) {
      mqttClient.publish("edilkamin/322707E4/hvac_mode/state",
                         (d.payload[4] & 1) ? "heat" : "off");
    } else if (currentOp == Helper::READ_ALARM) {
      char value[8];
      snprintf(value, sizeof(value), "%u", d.payload[3]);
      publishSensor(SensorDiscovery::Alarm, value);
    } else if (currentOp == Helper::READ_CANALIZED_2 || currentOp == Helper::READ_CANALIZED_3) {
      char value[12];
      StoveProtocol::temperature(StoveProtocol::word(d.payload + 3), value, sizeof(value));
      publishSensor(currentOp == Helper::READ_CANALIZED_2 ? SensorDiscovery::Channel2 :
                    SensorDiscovery::Channel3, value);
    } else if (currentOp == Helper::READ_NUMBER_FAN) {
      const byte previous = fans.equipment();
      fans.setEquipment(d.payload[4]);
      char code[4];
      snprintf(code, sizeof(code), "%u", d.payload[4]);
      mqttClient.publish("edilkamin/322707E4/fan_configuration/state", code, true);
      if (previous != fans.equipment())
        fanDiscoveryDirty = true;
      if (!fans.hasFan(2)) invalidateSensor(SensorDiscovery::Channel2);
      if (!fans.hasFan(3)) invalidateSensor(SensorDiscovery::Channel3);
      publishFanAvailability();
    } else if (currentOp == Helper::READ_FAN_23) {
      fans.updatePair(d.payload[3], d.payload[4]);
      publishExtraFanStates();
    } else if (currentOp == Helper::READ_AUTOMATIC) {
      if (d.payload[3] == 1) {
        automatic = true;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "ON");
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Auto");
      } else if (d.payload[3] == 0) {
        automatic = false;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "OFF");
      }
    } else if (currentOp == Helper::READ_POWER && !automatic) {
      currentPowerLevel = d.payload[4];
      switch (d.payload[4]) {
      case 1:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P1");
        break;
      case 2:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P2");
        break;
      case 3:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P3");
        break;
      case 4:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P4");
        break;
      case 5:
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P5");
        break;
      }
    } else if (currentOp == Helper::READ_MAIN_ENV_TEMP) {
      publishTemperature("edilkamin/322707E4/temperature/state", d.payload + 3);
    } else if (currentOp == Helper::READ_THERMOCOUPLE_TEMP) {
      publishTemperature("edilkamin/322707E4/thermocouple_temperature/state", d.payload + 3);
    } else if (currentOp == Helper::READ_TEMPERATURE) {
      publishTemperature("edilkamin/322707E4/target_temperature/state", d.payload + 3);
    } else if (currentOp == Helper::READ_FAN) {
      currentFan1Level = d.payload[3];
      currentPowerLevel = d.payload[4];
      fan1PowerValid = fan1PowerFresh =
          currentFan1Level <= 6 && currentPowerLevel <= 5;
      switch (d.payload[3]) {
      case 1:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "20%");
        break;
      case 2:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "40%");
        break;
      case 3:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "60%");
        break;
      case 4:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "80%");
        break;
      case 5:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "100%");
        break;
      case 6:
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "Auto");
        break;
      }
    } else if (currentOp == Helper::READ_RELAX) {
      if (d.payload[4] <= 1) {
        fans.setRelax(d.payload[4] == 1);
        publishFanAvailability();
      } else {
        handleBtFailure();
      }
      if (d.payload[4] == 0)
        mqttClient.publish("edilkamin/322707E4/relax/state", "OFF");
      else if (d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/relax/state", "ON");
    } else if (currentOp == Helper::READ_STANDBY_STATUS) {
      if (d.payload[3] == 0)
        mqttClient.publish("edilkamin/322707E4/standby/state", "OFF");
      else if (d.payload[3] == 1)
        mqttClient.publish("edilkamin/322707E4/standby/state", "ON");
    } else if (currentOp == Helper::READ_ON_OFF_CHRONO) {
      if (d.payload[4] == 0)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "OFF");
      else if (d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "ON");
    } else if (currentOp == Helper::READ_FIREPLACE_MAIN_STATUS) {
      char value[64];
      StoveProtocol::statusText(d.payload[3], d.payload[4], value, sizeof(value));
      mqttClient.publish("edilkamin/322707E4/status/state", value);
      snprintf(value, sizeof(value), "%u/%u", d.payload[4], d.payload[3]);
      publishSensor(SensorDiscovery::StatusCode, value);
    } else if (currentOp == Helper::READ_WARNING_FLAGS) {
      // Two registers = four data bytes. d.payload has only six bytes total;
      // use the validated decrypted frame for the fourth data byte.
      const byte *data = decrypted + 23;
      const bool reserve = StoveProtocol::pelletReserve(data);
      mqttClient.publish("edilkamin/322707E4/pellet_level/state", reserve ? "Reserve" : "Ok");
      publishSensor(SensorDiscovery::PelletReserve, reserve ? "ON" : "OFF");
      char value[12];
      snprintf(value, sizeof(value), "%02X%02X%02X%02X", data[0], data[1], data[2], data[3]);
      publishSensor(SensorDiscovery::Warnings, value);
    }
  }
  return true;
}

// MQTT-Callback
void mqttCallback(String topic, byte *message, unsigned int length) {
  debugln();
  debug(F("MQTT-Message arrived with topic: "));
  debug(topic);
  debug(F(". Message: "));
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    // debug((char)message[i]);
    msg += (char)message[i];
  }
  if (msg.length() != strlen(msg.c_str())) return; // Reject embedded NUL payloads.
  debug(msg);
  const bool switchTopic = topic == "edilkamin/322707E4/automatic_mode/set" ||
      topic == "edilkamin/322707E4/relax/set" || topic == "edilkamin/322707E4/chrono_mode/set" ||
      topic == "edilkamin/322707E4/standby/set" || topic == "edilkamin/322707E4/bluetooth/set";
  if (switchTopic && msg != "ON" && msg != "OFF") return;
  // debugln("");
  byte btCmd[6];
  if (topic == "homeassistant/status") {
    if (msg == "online")
      fanDiscoveryDirty = sensorDiscoveryDirty = true;
  } else if (topic == "edilkamin/322707E4/fan2_mode/set" ||
             topic == "edilkamin/322707E4/fan3_mode/set") {
    const byte fan = topic == "edilkamin/322707E4/fan2_mode/set" ? 2 : 3;
    if (!fans.request(fan, FanControl::parseMode(msg.c_str()))) {
      debugln(F("Rejected fan command: invalid mode, absent fan, or Relax active/unknown"));
      return;
    }
    // Also cancel a scheduled disconnect decided in the previous loop iteration.
    doBtConnect = true;
    if (fanRefreshStep == fanRefreshComplete && !fans.ready() && bleConnected)
      fanRefreshStep = 0;
  } else if (topic == "edilkamin/322707E4/bluetooth/set") {
    if (msg == "ON")
      doBtConnect = true;
    if (msg == "OFF")
      doBtConnect = false;
  } else if (topic == "edilkamin/322707E4/hvac_mode/set") {
    if (msg != "heat" && msg != "off") return;
    memcpy(btCmd, h.setOnOff, 6);
    if (msg == "heat")
      btCmd[5] = 1;
    else if (msg == "off")
      btCmd[5] = 0;
    queueBtCommand(Helper::SET_ON_OFF, btCmd);
  } else if (topic == "edilkamin/322707E4/fan_mode/set") {
    const byte level = FanControl::parseMode(msg.c_str());
    if (!level || !fan1PowerValid || !fans.canAccept(1))
      return;
    memcpy(btCmd, h.setFan1, 6);
    btCmd[4] = level;
    btCmd[5] = currentPowerLevel;
    queueBtCommand(Helper::SET_FAN_1, btCmd);
    if (bleConnected && !fan1PowerFresh && fanRefreshStep == fanRefreshComplete)
      fanRefreshStep = 0;
  } else if (topic == "edilkamin/322707E4/automatic_mode/set") {
    memcpy(btCmd, h.setChangeAutoSwitch, 6);
    if (msg == "ON")
      btCmd[4] = 1;
    else if (msg == "OFF")
      btCmd[4] = 0;
    queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);
  } else if (topic == "edilkamin/322707E4/preset_mode/set" && fan1PowerValid) {
    if (msg == "Auto") {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 1;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);
    } else {
      memcpy(btCmd, h.setWriteNewPower, 6);
      btCmd[4] = currentFan1Level;
      if (msg == "Man. P1")
        btCmd[5] = 1;
      else if (msg == "Man. P2")
        btCmd[5] = 2;
      else if (msg == "Man. P3")
        btCmd[5] = 3;
      else if (msg == "Man. P4")
        btCmd[5] = 4;
      else if (msg == "Man. P5")
        btCmd[5] = 5;
      else
        return;
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
  } else if (topic == "edilkamin/322707E4/target_temperature/set") {
    uint16_t t;
    if (!StoveProtocol::parseTemperature(msg.c_str(), t)) {
      debugln(F("Rejected temperature: expected decimal Celsius in range 5..35"));
      return;
    }
    memcpy(btCmd, h.setWriteNewTemperature, 6);
    btCmd[4] = (uint8_t)(t >> 8);
    btCmd[5] = (uint8_t)t;
    queueBtCommand(Helper::SET_WRITE_NEW_TEMP, btCmd);
  } else if (topic == "edilkamin/322707E4/relax/set") {
    memcpy(btCmd, h.setRelaxStatus, 6);
    if (msg == "ON") {
      btCmd[4] = 1;
      btCmd[5] = 1;
    } else if (msg == "OFF") {
      btCmd[4] = 0;
      btCmd[5] = 0;
    }
    queueBtCommand(Helper::SET_RELAX_STATUS, btCmd);
  } else if (topic == "edilkamin/322707E4/airkare/set") {
  } else if (topic == "edilkamin/322707E4/chrono_mode/set") {
    memcpy(btCmd, h.setOnOffChrono, 6);
    if (msg == "ON")
      btCmd[5] = 1;
    else if (msg == "OFF")
      btCmd[5] = 0;
    queueBtCommand(Helper::SET_ON_OFF_CHRONO, btCmd);
  } else if (topic == "edilkamin/322707E4/standby/set") {
    memcpy(btCmd, h.setStandbyStatus, 6);
    if (msg == "ON") {
      btCmd[4] = 1;
      btCmd[5] = 0;
    } else if (msg == "OFF") {
      btCmd[4] = 0;
      btCmd[5] = 0;
    }
    queueBtCommand(Helper::SET_STANDBY_STATUS, btCmd);
  }
}

// MQTT reconnect
void mqttReconnect() {
  debug(F("Attempting MQTT connection..."));
  debug(F(" Server: "));
  debug(mqtt_server);
  debug(F(" User: "));
  debugln(mqtt_user);
  if (mqttClient.connect(hostname, mqtt_user, mqtt_pass,
                         "edilkamin/322707E4/bridge/availability", 1, true,
                         "OFFLINE")) {
    mqttClient.publish("edilkamin/322707E4/bridge/availability", "ONLINE", true);
    mqttClient.publish("edilkamin/322707E4/firmware_version/state", EDILKAMIN_VERSION, true);
    publishFanAvailability();
    fanDiscoveryDirty = sensorDiscoveryDirty = true;
    msLastMqttConnect = millis(); // Reset watchdog on successful connect
    debugln(F("Connected. Subscripting to topics..."));
    mqttClient.subscribe("edilkamin/322707E4/fan_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/fan2_mode/set", 1);
    mqttClient.subscribe("edilkamin/322707E4/fan3_mode/set", 1);
    mqttClient.subscribe("homeassistant/status", 1);
    mqttClient.subscribe("edilkamin/322707E4/hvac_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/preset_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/target_temperature/set", true);
    mqttClient.subscribe("edilkamin/322707E4/relax/set", true);
    mqttClient.subscribe("edilkamin/322707E4/automatic_mode/set", true);
    // mqttClient.subscribe("edilkamin/322707E4/airkare/set");
    mqttClient.subscribe("edilkamin/322707E4/chrono_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/standby/set", true);
    mqttClient.subscribe("edilkamin/322707E4/bluetooth/set", true);

    debugln("Publishing Autodiscover Config to HA...");
    publishFanDiscovery();
    mqttClient.publish(
        "homeassistant/switch/edilkamin_322707E4_automatic_mode/config",
        h.jsonAutodiscoverAutomaticMode, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_relax/config",
                       h.jsonAutodiscoverRelax, true);
    // mqttClient.publish("homeassistant/switch/edilkamin_322707E4_airkare/config",
    // h.jsonAutodiscoverAirkare);
    mqttClient.publish(
        "homeassistant/switch/edilkamin_322707E4_chrono_mode/config",
        h.jsonAutodiscoverCronoMode, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_standby/config",
                       h.jsonAutodiscoverStandbyMode, true);
    mqttClient.publish("homeassistant/sensor/edilkamin_322707E4_status/config",
                       h.jsonAutodiscoverStatus, true);
    mqttClient.publish(
        "homeassistant/switch/edilkamin_322707E4_bluetooth/config",
        h.jsonAutodiscoverBluetooth, true);
    mqttClient.publish(
        "homeassistant/sensor/edilkamin_322707E4_thermocouple_temp/config",
        h.jsonAutodiscoverThermocouple, true);
    mqttClient.publish(
        "homeassistant/sensor/edilkamin_322707E4_pellet_level/config",
        h.jsonAutodiscoverPelletLevel, true);
  } else {
    debug(F("failed, rc="));
    debug(mqttClient.state());
  }
  //}
}

static uint8_t btDataBuffer[32];

static void bleNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
                              uint8_t *pData, size_t length, bool isNotify) {
  if (length != sizeof(btDataBuffer))
    return;
  memcpy(btDataBuffer, pData, sizeof(btDataBuffer));
  btResponse = true;
  btData = btDataBuffer;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {}

  void onDisconnect(BLEClient *pclient) {
    bleConnected = false;
    bleDisconnected = true;
    debugln();
    debugln(F("BLE disconnected..."));
  }
};

static MyClientCallback myClientCallback;

bool connectToServer() {
  debug(F("Forming a connection to "));
  debugln(myDevice->getAddress().toString().c_str());

  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    debugln(F(" - Created client"));
    pClient->setClientCallbacks(&myClientCallback);
  }

  // Connect to the remote BLE Server.
  if (!pClient->connect(myDevice)) {
    debugln(F(" - Failed to connect to server"));
    return false;
  }
  debugln(F(" - Connected to server"));

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    debug(F("Failed to find our service UUID: "));
    debugln(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  debugln(F(" - Found our service"));

  // Obtain a reference to the characteristic in the service of the remote BLE
  // server.
  pRemoteCharacteristicWrite = pRemoteService->getCharacteristic(charUUIDWrite);
  pRemoteCharacteristicRead = pRemoteService->getCharacteristic(charUUIDRead);
  if (pRemoteCharacteristicWrite == nullptr ||
      pRemoteCharacteristicRead == nullptr) {
    debug(F("Failed to find our characteristic UUID: "));
    // debugln(charUUIDWrite.toString().c_str());
    pClient->disconnect();
    return false;
  }
  debugln(F(" - Found our characteristic"));

  if (pRemoteCharacteristicRead->canNotify()) {
    pRemoteCharacteristicRead->subscribe(true, bleNotifyCallback, false);
  }

  bleConnected = true;
  bleDisconnected = false;
  bleLastConnect = millis();
  fans.requireRefresh();
  fan1PowerFresh = false;
  fanRefreshStep = 0;
  btResponse = false;
  status = CHECK_BT_WRITE_QUEUE;
  debugln(F("Publishing Online-state to HA..."));
  mqttClient.publish("edilkamin/322707E4/availability/state", "ONLINE", true);
  return true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we
 * are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice *advertisedDevice) {
    debug(F("BLE Advertised Device found: "));
    debugln(advertisedDevice->toString().c_str());

    if (advertisedDevice->haveServiceUUID() &&
        advertisedDevice->isAdvertisingService(serviceUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = advertisedDevice; /** Just save the reference now, no need to
                                      copy the object */
      bleDoConnect = true;
      bleDoScan = true;
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Edilkamin_BT " EDILKAMIN_VERSION);

  // Check if Boot Button (GPIO 0) is pressed for factory reset
  pinMode(0, INPUT_PULLUP);
  // Wait to ensure it's a deliberate press (simple debounce/hold check)
  if (digitalRead(0) == LOW) {
    delay(500); // Wait 500ms
    if (digitalRead(0) == LOW) {
      Serial.println("Factory Reset initiated...");

      // Mount FS to delete config
      if (LittleFS.begin(true)) {
        if (LittleFS.exists("/config.json")) {
          LittleFS.remove("/config.json");
          Serial.println("Deleted config.json");
        }
        LittleFS.end();
      }

      WiFiManager wm;
      wm.resetSettings();
      Serial.println("WiFiManager settings reset");

      Serial.println("Restarting...");
      delay(1000);
      ESP.restart();
    }
  }

  // Mount LittleFS
  if (LittleFS.begin(true)) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      // file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        JsonDocument json;
        DeserializationError error = deserializeJson(json, configFile);
        if (!error) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
          strcpy(ntp_server, json["ntp_server"]);
          strcpy(ntp_offset, json["ntp_offset"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server,
                                          40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 32);
  WiFiManagerParameter custom_ntp_server("ntp", "ntp server", ntp_server, 40);
  WiFiManagerParameter custom_ntp_offset("ntp_offset", "ntp offset", ntp_offset,
                                         6);

  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_ntp_server);
  wifiManager.addParameter(&custom_ntp_offset);

  wifiManager.setHostname(hostname);
  wifiManager.setTitle("Configuration");

  if (!wifiManager.autoConnect("Edilkamin_BT_AP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }

  Serial.println("connected.");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  strcpy(ntp_server, custom_ntp_server.getValue());
  strcpy(ntp_offset, custom_ntp_offset.getValue());

  if (shouldSaveConfig) {
    Serial.println("saving config");
    JsonDocument json;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;
    json["ntp_server"] = ntp_server;
    json["ntp_offset"] = ntp_offset;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, configFile);
    configFile.close();
    Serial.println("config saved");
  }

  h.getNtpTime(ntp_server, atol(ntp_offset));
  TelnetStream.begin();
  mqttClient.setServer(mqtt_server, atoi(mqtt_port));
  mqttClient.setBufferSize(2048);
  mqttClient.setCallback(mqttCallback);
  msLastMqttConnect = millis(); // Reset MQTT watchdog timer
  msLastBleConnect = millis();
  BLEDevice::init("");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop() {
  currentMillis = millis();
  if (bleDisconnected) {
    bleDisconnected = false;
    controlTransaction.cancel();
    fans.failWrite();
    fans.requireRefresh();
    fan1PowerFresh = false;
    fanRefreshStep = fanRefreshComplete;
    btResponse = false;
    status = START;
  }

  // Telnet-Terminal
  switch (TelnetStream.read()) {
  case 'c':
    debugln("Bye...");
    TelnetStream.flush();
    TelnetStream.stop();
    break;
  }

  // BLE Watchdog - Restart ESP if no connect for 5min
  if (!bleConnected) {
    if (currentMillis - msLastBleConnect >= bleConnectTimeout) {
      debugln(F("BLE not connected watchdog triggered - ESP Restart..."));
      ESP.restart();
    }
  } else {
    msLastBleConnect = currentMillis;
  }

  // MQTT Watchdog - Restart ESP if no connect to MQTT-Server
  if (!mqttClient.loop() || !mqttClient.connected()) {
    mqttReconnect();
    if (!mqttClient.connected() &&
        (currentMillis - msLastMqttConnect >= connectTimeout)) {
      msLastMqttConnect = currentMillis;
      debugln(F("MQTT not connected watchdog triggered - ESP Restart..."));
      ESP.restart();
    }
  } else {
    msLastMqttConnect = currentMillis;
  }

  for (unsigned i = 0; i < SensorDiscovery::Count; ++i)
    if (sensorValid[i] && currentMillis - sensorUpdated[i] >= sensorMaxAge)
      invalidateSensor(static_cast<SensorDiscovery::Id>(i));
  if (sensorDiscoveryDirty && mqttClient.connected())
    publishSensorDiscovery();

  if (fanDiscoveryDirty && mqttClient.connected())
    publishFanDiscovery();

  if (bleDoConnect == true) {
    if (connectToServer()) {
      debugln(F("We are now connected to the BLE Server."));
      mqttClient.publish("edilkamin/322707E4/bluetooth/state", "ON");
    } else
      debugln(F("Failed to connect to the MQTT-Server; there is nothing more "
                "we will do."));
    bleDoConnect = false;
  }

  if (!doBtConnect && bleConnected) {
    if (pClient->isConnected()) {
      pClient->disconnect();
      // mqttClient.publish("edilkamin/322707E4/availability/state",
      // "OFFLINE", true);
      mqttClient.publish("edilkamin/322707E4/bluetooth/state", "OFF", true);
    }
  } else if (!bleConnected && doBtConnect) {
    BLEDevice::getScan()->start(5, false);
    mqttClient.publish("edilkamin/322707E4/bluetooth/state", "ON", true);
    status = START;
  }

  switch (status) {
  case START:
    status = CHECK_BT_WRITE_QUEUE;
    break;
  case DO_NEXT_QUERY:
    if (writeQueueHasElements()) {
      status = CHECK_BT_WRITE_QUEUE;
    } else if (currentMillis - msLastQuery >= queryInterval) {
      msLastQuery = currentMillis;
      nextQuery();
      status = CHECK_BT_WRITE_QUEUE;
    }
    break;
  case CHECK_BT_WRITE_QUEUE:
    if (writeQueueHasElements())
      status = BT_CONNECT_CHECK;
    else
      status = BT_CHECK_ON_OFF_TIMES;
    break;
  case BT_CHECK_ON_OFF_TIMES:
    if (writeQueueHasElements()) {
      status = BT_CONNECT_CHECK;
    } else if (bleConnected && currentMillis - bleLastConnect >= bleOnTime) {
      doBtConnect = false;
      bleLastSwitchOff = currentMillis;
      status = START;
    } else if (bleConnected) {
      status = DO_NEXT_QUERY;
    } else if (!bleConnected && currentMillis - bleLastSwitchOff >= bleOffTime) {
      debugln(F("Bluetooth wake up..."));
      status = DO_NEXT_QUERY;
    } else if (!bleConnected) {
      status = START;
    }
    break;
  case BT_CONNECT_CHECK:
    if (bleConnected)
      status = BT_WRITE_REQUEST;
    else {
      status = BT_CONNECT_CHECK;
      doBtConnect = true;
      bleLastConnect = currentMillis;
    }
    break;
  case BT_WRITE_REQUEST:
    btResponse = false;
    if (!writeBtData()) {
      status = START;
      break;
    }
    writeTimestamp = currentMillis;
    status = AWAIT_BT_RESPONSE;
    break;
  case AWAIT_BT_RESPONSE:
    if (btResponse) {
      status = PROCESSING_BT_RESPONSE;
    } else if (currentMillis - writeTimestamp >= responseTimeout) {
      debug(F("Bluetooth Response Timeout for cmd "));
      debugln(currentOp);
      handleBtFailure();
      // Read replies contain no register address. Reset the link before the
      // next request so a delayed reply cannot confirm a different shared read.
      // Writes are not blindly repeated after an uncertain acknowledgement.
      pClient->disconnect();
      status = START;
    }
    break;
  case PROCESSING_BT_RESPONSE:
    btResponse = false;
    status = processBtResponseData(btData) ? DONE_PROCESSING_BT_RESPONSE
                                          : AWAIT_BT_RESPONSE;
    break;
  case DONE_PROCESSING_BT_RESPONSE:
    status = START;
    break;
  };
}
