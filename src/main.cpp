#include <Arduino.h>
#include <ArduinoJson.h>
#include <Helper.h>
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
uint8_t btRetryCount = 0;
const uint8_t btMaxRetries = 1;
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

byte currentFan1Level = 0;
byte currentPowerLevel = 0;
bool automatic = false;

struct btWriteMapping {
  Helper::btCmds name;
  byte cmd[6];
};

uint8_t btWriteQueueIndex = 0;
const uint8_t btWriteQueueLength = 10;
btWriteMapping btWriteQueue[btWriteQueueLength] = {
    {Helper::NO_CMD, 0}, {Helper::NO_CMD, 0}, {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0}, {Helper::NO_CMD, 0}, {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0}, {Helper::NO_CMD, 0}, {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0}};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Helper::btCmds currentOp;

struct queryLookup {
  const Helper::btCmds name;
  const byte *cmd;
};
uint8_t queryIndex = 0;
const uint8_t queryQueueElements = 11;
const queryLookup queryQueue[] = {
    {Helper::READ_POWER, h.readPower},
    {Helper::READ_AUTOMATIC, h.readAutomatic},
    {Helper::READ_ON_OFF_CHRONO, h.readOnOffChrono},
    {Helper::READ_MAIN_ENV_TEMP, h.readMainEnvTemp},
    {Helper::READ_THERMOCOUPLE_TEMP, h.readThermocoupleTemperature},
    {Helper::READ_TEMPERATURE, h.readTemperature},
    {Helper::READ_FAN, h.readFan},
    {Helper::READ_RELAX, h.readRelax},
    {Helper::READ_FIREPLACE_MAIN_STATUS, h.readFireplaceMainStatus},
    {Helper::READ_STANDBY_STATUS, h.readStandbyStatus},
    {Helper::READ_WARNING_FLAGS, h.readWarningFlags}};

void hexDebug(byte *data, size_t length) {
  char output[(length * 2) + 1];
  char *ptr = &output[0];
  int i;
  for (i = 0; i < length; i++) {
    ptr += sprintf(ptr, "%02X", (int)data[i]);
  }
  debug(output);
}

void queueBtCommand(Helper::btCmds name, byte *cmd) {
  for (uint8_t i = 0; i < btWriteQueueLength; i++) {
    if (btWriteQueue[i].name == Helper::NO_CMD) {
      btWriteQueue[i].name = name;
      memcpy(btWriteQueue[i].cmd, cmd, 6);
      debug(F(", queued at index:"));
      debug(i);
      debugln();
      break;
    }
  }
}

void nextQuery() {
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
  for (uint8_t i = 0; i < btWriteQueueLength; i++) {
    if (btWriteQueue[i].name != Helper::NO_CMD)
      return true;
  }
  return false;
}

static byte lastBtPacket[32];

void writeBtData() {
  for (uint8_t i = 0; i < btWriteQueueLength; i++) {
    if (btWriteQueue[i].name != Helper::NO_CMD) {
      currentOp = btWriteQueue[i].name;
      debug(F("-> Write, queue-index:"));
      debug(i);
      debug(F(", "));
      debug(F("cmd:"));
      debug(currentOp);
      debug(", data:");
      h.createBtPacket(btWriteQueue[i].cmd, 6, lastBtPacket);
      hexDebug(btWriteQueue[i].cmd, 6);
      debugln();
      btWriteQueue[i].name = Helper::NO_CMD;
      pRemoteCharacteristicWrite->writeValue(lastBtPacket, 32);
      break;
    }
  }
}
void processBtResponseData(byte *btData) {
  debug(F("<- Read, data:"));
  Helper::structDatagram d;
  h.getBtContent(btData, 32, &d);
  hexDebug(d.payload, 6);
  debugln();
  if (d.payload[1] == 6) { // Set Response
    debug(F("Set-Response for cmd:"));
    debug(currentOp);
    debug("; data:");
    hexDebug(d.payload, 6);
    debugln("");
    if (currentOp == Helper::SET_ON_OFF) {
      if (d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/hvac_mode/state", "heat");
      else if (d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/hvac_mode/state", "off");
    } else if (currentOp == Helper::SET_RELAX_STATUS) {
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
      else if (d.payload[4] == 1 && d.payload[5] == 1)
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
      uint16_t r = (d.payload[4] << 8) + d.payload[5];
      float x = r * 0.1;
      char msg_out[10];
      dtostrf(x, 2, 1, msg_out);
      mqttClient.publish("edilkamin/322707E4/target_temperature/state",
                         msg_out);
    }
  } else if (d.payload[1] == 3) { // Query Response
    debug(F("Response for query-cmd:"));
    debug(currentOp);
    debug("; data:");
    hexDebug(d.payload, 6);

    debugln();
    if (currentOp == Helper::READ_AUTOMATIC) {
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
      int r = (int16_t)(0 << 8) + d.payload[4];
      if (r > 0) {
        float x = r * 0.1;
        char msg_out[10];
        dtostrf(x, 2, 1, msg_out);
        mqttClient.publish("edilkamin/322707E4/temperature/state", msg_out);
      }
    } else if (currentOp == Helper::READ_THERMOCOUPLE_TEMP) {
      uint16_t r = (d.payload[3] << 8) + d.payload[4];
      r = (r * 0.1) + 0.5;
      char msg_out[10];
      itoa(r, msg_out, 10);
      mqttClient.publish("edilkamin/322707E4/thermocouple_temperature/state",
                         msg_out);
    } else if (currentOp == Helper::READ_TEMPERATURE) {
      uint16_t r = (d.payload[3] << 8) + d.payload[4];
      float x = r * 0.1;
      char msg_out[10];
      dtostrf(x, 2, 1, msg_out);
      mqttClient.publish("edilkamin/322707E4/target_temperature/state",
                         msg_out);
    } else if (currentOp == Helper::READ_FAN) {
      currentFan1Level = d.payload[3];
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
      if (d.payload[3] == 1 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Cleaning");
      else if (d.payload[3] == 3 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state",
                           "Loading Pellets");
      else if (d.payload[3] == 4 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state",
                           "Ignition - charge suspend");
      else if (d.payload[3] == 5 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state",
                           "Check exhaust gas temperature");
      else if (d.payload[3] == 6 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state",
                           "Threshold temperature check");
      else if (d.payload[3] == 7 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Warmup");
      else if (d.payload[3] == 2 && d.payload[4] == 2)
        mqttClient.publish("edilkamin/322707E4/status/state", "Ok");
      else if (d.payload[3] == 0 && d.payload[4] == 3)
        mqttClient.publish("edilkamin/322707E4/status/state", "Shutdown");
      else if (d.payload[3] == 0 && d.payload[4] == 4)
        mqttClient.publish("edilkamin/322707E4/status/state", "Extinction");
      else if (d.payload[3] == 0 && d.payload[4] == 0) {
        mqttClient.publish("edilkamin/322707E4/status/state", "Off");
        mqttClient.publish("edilkamin/322707E4/hvac_mode/state", "off");
      } else
        mqttClient.publish("edilkamin/322707E4/status/state", "-");
    } else if (currentOp == Helper::READ_WARNING_FLAGS) {
      if (d.payload[4] == 0) // Pellet ok
        mqttClient.publish("edilkamin/322707E4/pellet_level/state", "Ok");
      else if (d.payload[4] == 4) // Pellet low
        mqttClient.publish("edilkamin/322707E4/pellet_level/state", "Empty");
    }
  }
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
  debug(msg);
  // debugln("");
  byte btCmd[6];
  if (topic == "edilkamin/322707E4/bluetooth/set") {
    if (msg == "ON")
      doBtConnect = true;
    if (msg == "OFF")
      doBtConnect = false;
  } else if (topic == "edilkamin/322707E4/hvac_mode/set") {
    memcpy(btCmd, h.setOnOff, 6);
    if (msg == "heat")
      btCmd[5] = 1;
    else if (msg == "off")
      btCmd[5] = 0;
    queueBtCommand(Helper::SET_ON_OFF, btCmd);
  } else if (topic == "edilkamin/322707E4/fan_mode/set" &&
             currentFan1Level != 0 &&
             currentPowerLevel !=
                 0) // ignore set command if fan & power-level is 0 (=boot)
  {
    byte btPacket[32];
    memcpy(btCmd, h.setFan1, 6);
    btCmd[5] = currentPowerLevel;
    if (msg == "20%")
      btCmd[4] = 1;
    else if (msg == "40%")
      btCmd[4] = 2;
    else if (msg == "60%")
      btCmd[4] = 3;
    else if (msg == "80%")
      btCmd[4] = 4;
    else if (msg == "100%")
      btCmd[4] = 5;
    else if (msg == "Auto")
      btCmd[4] = 6;
    queueBtCommand(Helper::SET_FAN_1, btCmd);
  } else if (topic == "edilkamin/322707E4/automatic_mode/set") {
    byte btPacket[32];
    memcpy(btCmd, h.setChangeAutoSwitch, 6);
    if (msg == "ON")
      btCmd[4] = 1;
    else if (msg == "OFF")
      btCmd[4] = 0;
    queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);
  } else if (topic == "edilkamin/322707E4/preset_mode/set" &&
             currentFan1Level != 0 &&
             currentPowerLevel !=
                 0) // ignore set command if fan & power-level is 0 (=boot)
  {
    byte btPacket[32];
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
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
  } else if (topic == "edilkamin/322707E4/target_temperature/set") {
    msg.replace(".", ""); // 24.5 to 245
    uint16_t t = msg.toInt();
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
      btCmd[5] = 1;
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
  if (mqttClient.connect(hostname, mqtt_user, mqtt_pass)) {
    msLastMqttConnect = millis(); // Reset watchdog on successful connect
    debugln(F("Connected. Subscripting to topics..."));
    mqttClient.subscribe("edilkamin/322707E4/fan_mode/set", true);
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
    mqttClient.publish("homeassistant/climate/edilkamin_322707E4/config",
                       h.jsonAutodiscover, true);
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
  memcpy(btDataBuffer, pData, min(length, (size_t)32));
  btResponse = true;
  btData = btDataBuffer;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {}

  void onDisconnect(BLEClient *pclient) {
    bleConnected = false;
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
    if (currentMillis - msLastQuery >= queryInterval) {
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
    if (bleConnected && currentMillis - bleLastConnect >= bleOnTime) {
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
    btRetryCount = 0;
    writeBtData();
    writeTimestamp = currentMillis;
    status = AWAIT_BT_RESPONSE;
    break;
  case AWAIT_BT_RESPONSE:
    if (btResponse) {
      status = PROCESSING_BT_RESPONSE;
    } else if (currentMillis - writeTimestamp >= responseTimeout) {
      if (btRetryCount < btMaxRetries) {
        btRetryCount++;
        debug(F("Bluetooth Response Timeout, retry "));
        debug(btRetryCount);
        debug(F("/"));
        debugln(btMaxRetries);
        btResponse = false;
        pRemoteCharacteristicWrite->writeValue(lastBtPacket, 32);
        writeTimestamp = currentMillis;
      } else {
        debug(F("Bluetooth Response Timeout for cmd "));
        debugln(currentOp);
        status = START;
      }
    }
    break;
  case PROCESSING_BT_RESPONSE:
    processBtResponseData(btData);
    status = DONE_PROCESSING_BT_RESPONSE;
    break;
  case DONE_PROCESSING_BT_RESPONSE:
    status = START;
    break;
  };
}
