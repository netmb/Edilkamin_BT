#include <Arduino.h>
#include <Helper.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>

/* WLAN-Setup */

const char *hostname PROGMEM = "edilkaminble";
const char *ssid PROGMEM = WLAN_SSID;
const char *password PROGMEM = WLAN_PASSWORD;

/* MQTT Setup*/

const char *mqttHost PROGMEM = MQTT_HOST;
const char *mqttUser PROGMEM = MQTT_USER;
const char *mqttPass PROGMEM = MQTT_PASSWORD;

enum states
{
  START,
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
unsigned long previousMillis = 0;
unsigned long previousMillisConnect = 0;
unsigned long writeTimestamp = 0;
const long queryInterval = 1000;
const long responseTimeout = 1000;
const long connectTimeout = 30000;

struct btWriteMapping
{
  Helper::btCmds name;
  byte cmd[6];
};

uint8_t btWriteQueueIndex = 0;
const uint8_t btWriteQueueLength = 10;
btWriteMapping btWriteQueue[btWriteQueueLength] = {
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0},
    {Helper::NO_CMD, 0}};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Helper::btCmds currentOp;

struct queryLookup
{
  const Helper::btCmds name;
  const byte *cmd;
};
uint8_t queryIndex = 0;
const uint8_t queryQueueElements = 16;
const queryLookup queryQueue[] = {
    {Helper::READ_POWER, h.readPower},
    {Helper::READ_AUTOMATIC, h.readAutomatic},
    {Helper::READ_ON_OFF_CHRONO, h.readOnOffChrono},
    {Helper::READ_MAIN_ENV_TEMP, h.readMainEnvTemp},
    {Helper::READ_THERMOCOUPLE_TEMP, h.readThermocoupleTemperature},
    {Helper::READ_TEMPERATURE, h.readTemperature},
    {Helper::READ_FAN_MACHINE, h.readFanMachine},
    {Helper::READ_FAN, h.readFan},
    {Helper::READ_RELAX, h.readRelax},
    {Helper::READ_FIREPLACE_CURRENT_POWER, h.readFireplaceCurrentPower},
    {Helper::READ_PELLET_SENSOR, h.readPelletSensor},
    {Helper::READ_PELLET_REMAINING, h.readPelletRemaining},
    {Helper::READ_ECO_TEMP, h.readEconomyTemperature},
    {Helper::READ_COMFORT_TEMP, h.readComfortTemperature},
    {Helper::READ_FIREPLACE_MAIN_STATUS, h.readFireplaceMainStatus},
    {Helper::READ_STANDBY_STATUS, h.readStandbyStatus}};

bool automatic = false;

void queueBtCommand(Helper::btCmds name, byte *cmd)
{
  for (uint8_t i = 0; i < btWriteQueueLength; i++)
  {
    if (btWriteQueue[i].name == Helper::NO_CMD)
    {
      btWriteQueue[i].name = name;
      memcpy(btWriteQueue[i].cmd, cmd, 6);
      Serial.print(F(",empty queue place found at:"));
      Serial.print(i);
      Serial.println();
      break;
    }
  }
}

void nextQuery()
{
  byte btCmd[6];
  Serial.print(F("nextQuery:"));
  Serial.print(queryQueue[queryIndex].name);
  Serial.print(F(",command:"));
  h.hexDebug((unsigned char *)queryQueue[queryIndex].cmd, 6);
  memcpy(btCmd, queryQueue[queryIndex].cmd, 6);
  queueBtCommand(queryQueue[queryIndex].name, btCmd);

  if (queryIndex < queryQueueElements - 1)
  {
    queryIndex++;
  }
  else
  {
    queryIndex = 0;
  }
}

bool writeQueueHasElements()
{
  for (uint8_t i = 0; i < btWriteQueueLength; i++)
  {
    if (btWriteQueue[i].name != Helper::NO_CMD)
      return true;
  }
  return false;
}

void writeBtData()
{
  /*
  Serial.println("--- Current Write Queue ---");
  for (uint8_t i = 0 ; i < 10; i++) {
    Serial.print("btCmdQueue:");
    Serial.print(i);
    Serial.print(",");
    Serial.print(btWriteQueue[i].name);
    Serial.print(",");
    if (btWriteQueue[i].name != Helper::NO_CMD) {
      h.hexDebug(btWriteQueue[i].cmd, 6);
    }
    Serial.println();
  }
  Serial.println("--- End Current Write Queue ---");
  */
  for (uint8_t i = 0; i < btWriteQueueLength; i++)
  {
    if (btWriteQueue[i].name != Helper::NO_CMD)
    {
      currentOp = btWriteQueue[i].name;
      Serial.print(F("writeBtData:"));
      byte btPacket[32];
      Serial.print(F("Index:"));
      Serial.print(i);
      Serial.print(F(","));
      Serial.print(F("CommandNameIndex:"));
      Serial.print(currentOp);
      Serial.print(",");
      h.createBtPacket(btWriteQueue[i].cmd, 6, btPacket);
      h.hexDebug(btWriteQueue[i].cmd, 6);
      Serial.println();
      btWriteQueue[i].name = Helper::NO_CMD;
      pRemoteCharacteristicWrite->writeValue(btPacket, 32);
      break;
    }
  }
}
void processBtResponseData(byte *btData)
{
  Serial.print(F("ProcessBtResponseData:"));
  Helper::structDatagram d;
  h.getBtContent(btData, 32, &d);
  h.hexDebug(d.payload, 6);
  Serial.println();
  if (d.payload[1] == 6)
  { // Set Response
    Serial.print(F("SetResponse:"));
    Serial.print(currentOp);
    Serial.print(";");
    h.hexDebug(d.payload, 6);
    if (currentOp == Helper::SET_ON_OFF)
    {
      if (d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/hvac_mode/state", "heat");
      else if (d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/hvac_mode/state", "off");
    }
    else if (currentOp == Helper::SET_RELAX_STATUS)
    {
      if (d.payload[4] == 0 && d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/relax/state", "OFF");
      else if (d.payload[4] == 1 && d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/relax/state", "ON");
    }
    else if (currentOp == Helper::SET_WRITE_NEW_POWER && automatic)
    {
      if (d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P1");
      else if (d.payload[5] == 2)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P2");
      if (d.payload[5] == 3)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P3");
      if (d.payload[5] == 4)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P4");
      if (d.payload[5] == 5)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P5");
    }
    else if (currentOp == Helper::SET_FAN_1)
    {
      if (d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "20%");
      else if (d.payload[4] == 2)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "40%");
      else if (d.payload[4] == 3)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "60%");
      else if (d.payload[4] == 4)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "80%");
      else if (d.payload[4] == 5)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "100%");
      else if (d.payload[4] == 6)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "Auto");
    }
    else if (currentOp == Helper::SET_ON_OFF_CHRONO)
    {
      if (d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "OFF");
      else if (d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "ON");
    }
    else if (currentOp == Helper::SET_STANDBY_STATUS)
    {
      if (d.payload[4] == 0 && d.payload[5] == 0)
        mqttClient.publish("edilkamin/322707E4/standby/state", "OFF");
      else if (d.payload[4] == 1 && d.payload[5] == 1)
        mqttClient.publish("edilkamin/322707E4/standby/state", "ON");
    }
    else if (currentOp == Helper::SET_CHANGE_AUTO_SWITCH)
    {
      if (d.payload[4] == 0)
        automatic = false;
      else if (d.payload[4] == 1)
      {
        automatic = true;
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Auto");
      }
    }
    else if (currentOp == Helper::SET_WRITE_NEW_TEMP)
    {
      uint16_t r = (d.payload[4] << 8) + d.payload[5];
      float x = r * 0.1;
      char msg_out[10];
      dtostrf(x, 2, 1, msg_out);
      mqttClient.publish("edilkamin/322707E4/target_temperature/state", msg_out);
    }
  }
  else if (d.payload[1] == 3)
  { // Query Response
    Serial.print(F("Parsed Response:"));
    Serial.print(currentOp);
    Serial.print(";");
    h.hexDebug(d.payload, 6);
    Serial.println();
    if (currentOp == Helper::READ_AUTOMATIC)
    {
      if (d.payload[3] == 1)
      {
        automatic = true;
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Auto");
      }
      else if (d.payload[3] == 0)
        automatic = false;
    }
    else if (currentOp == Helper::READ_POWER && !automatic)
    {
      if (d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P1");
      else if (d.payload[4] == 2)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P2");
      else if (d.payload[4] == 3)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P3");
      else if (d.payload[4] == 4)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P4");
      else if (d.payload[4] == 5)
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Man. P5");
    }
    else if (currentOp == Helper::READ_MAIN_ENV_TEMP)
    {
      int r = (int16_t)(0 << 8) + d.payload[4];
      float x = r * 0.1;
      char msg_out[10];
      dtostrf(x, 2, 1, msg_out);
      mqttClient.publish("edilkamin/322707E4/temperature/state", msg_out);
    }
    else if (currentOp == Helper::READ_THERMOCOUPLE_TEMP)
    {
      uint16_t r = (d.payload[3] << 8) + d.payload[4];
      float x = r * 0.1;
      char msg_out[10];
      dtostrf(x, 2, 1, msg_out);
      mqttClient.publish("edilkamin/322707E4/thermocouple_temperature/state", msg_out);
    }
    else if (currentOp == Helper::READ_TEMPERATURE)
    {
      uint16_t r = (d.payload[3] << 8) + d.payload[4];
      float x = r * 0.1;
      char msg_out[10];
      dtostrf(x, 2, 1, msg_out);
      mqttClient.publish("edilkamin/322707E4/target_temperature/state", msg_out);
    }
    else if (currentOp == Helper::READ_FAN)
    {
      if (d.payload[3] == 1)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "20%");
      else if (d.payload[3] == 2)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "40%");
      else if (d.payload[3] == 3)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "60%");
      else if (d.payload[3] == 4)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "80%");
      else if (d.payload[3] == 5)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "100%");
      else if (d.payload[3] == 6)
        mqttClient.publish("edilkamin/322707E4/fan_mode/state", "Auto");
    }
    else if (currentOp == Helper::READ_RELAX)
    {
      if (d.payload[4] == 0)
        mqttClient.publish("edilkamin/322707E4/relax/state", "OFF");
      else if (d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/relax/state", "ON");
    }
    else if (currentOp == Helper::READ_STANDBY_STATUS)
    {
      if (d.payload[3] == 0)
        mqttClient.publish("edilkamin/322707E4/standby/state", "OFF");
      else if (d.payload[3] == 1)
        mqttClient.publish("edilkamin/322707E4/standby/state", "ON");
    }
    else if (currentOp == Helper::READ_ON_OFF_CHRONO)
    {
      if (d.payload[4] == 0)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "OFF");
      else if (d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/chrono_mode/state", "ON");
    }
    else if (currentOp == Helper::READ_FIREPLACE_MAIN_STATUS)
    {
      if (d.payload[3] == 1 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Cleaning");
      else if (d.payload[3] == 3 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Loading Pellets");
      else if (d.payload[3] == 4 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Ignition - charge suspend");
      else if (d.payload[3] == 5 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Check exhaust gas temperature");
      else if (d.payload[3] == 6 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Threshold temperature check");
      else if (d.payload[3] == 7 && d.payload[4] == 1)
        mqttClient.publish("edilkamin/322707E4/status/state", "Warmup");
      else if (d.payload[3] == 2 && d.payload[4] == 2)
        mqttClient.publish("edilkamin/322707E4/status/state", "Ok");
      else if (d.payload[3] == 0 && d.payload[4] == 3)
        mqttClient.publish("edilkamin/322707E4/status/state", "Shutdown");
      else if (d.payload[3] == 0 && d.payload[4] == 4)
        mqttClient.publish("edilkamin/322707E4/status/state", "Extinction");
      else if (d.payload[3] == 0 && d.payload[4] == 0)
      {
        mqttClient.publish("edilkamin/322707E4/status/state", "Off");
        mqttClient.publish("edilkamin/322707E4/hvac_mode/state", "off");
      }
      else
        mqttClient.publish("edilkamin/322707E4/status/state", "-");
    }
  }
}

// MQTT-Callback
void mqttCallback(String topic, byte *message, unsigned int length)
{
  Serial.print(F("Message arrived on topic: "));
  Serial.print(topic);
  Serial.print(F(". Message: "));
  String msg;
  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    msg += (char)message[i];
  }
  Serial.println("");
  byte btCmd[6];
  if (topic == "edilkamin/322707E4/bluetooth/set")
  {
    if (msg == "ON")
    {
      doBtConnect = true;
    }
    else if (msg == "OFF")
    {
      doBtConnect = false;
    }
  }
  else if (topic == "edilkamin/322707E4/hvac_mode/set")
  {
    memcpy(btCmd, h.setOnOff, 6);
    if (msg == "heat")
    {
      btCmd[5] = 1;
    }
    else if (msg == "off")
    {
      btCmd[5] = 0;
    }
    queueBtCommand(Helper::SET_ON_OFF, btCmd);
  }
  else if (topic == "edilkamin/322707E4/fan_mode/set")
  {
    byte btPacket[32];
    if (msg == "20%")
    {
      memcpy(btCmd, h.setFan1, 6);
      btCmd[4] = 1;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_FAN_1, btCmd);
    }
    else if (msg == "40%")
    {
      memcpy(btCmd, h.setFan1, 6);
      btCmd[4] = 2;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_FAN_1, btCmd);
    }
    else if (msg == "60%")
    {
      memcpy(btCmd, h.setFan1, 6);
      btCmd[4] = 3;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_FAN_1, btCmd);
    }
    else if (msg == "80%")
    {
      memcpy(btCmd, h.setFan1, 6);
      btCmd[4] = 4;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_FAN_1, btCmd);
    }
    else if (msg == "100%")
    {
      memcpy(btCmd, h.setFan1, 6);
      btCmd[4] = 5;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_FAN_1, btCmd);
    }
    else if (msg == "Auto")
    {
      memcpy(btCmd, h.setFan1, 6);
      btCmd[4] = 6;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_FAN_1, btCmd);
    }
  }
  else if (topic == "edilkamin/322707E4/preset_mode/set")
  {
    byte btPacket[32];
    if (msg == "Auto")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 1;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);
    }
    else if (msg == "Man. P1")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 0;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);

      memcpy(btCmd, h.setWriteNewPower, 6);
      btCmd[4] = 2;
      btCmd[5] = 1;
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
    else if (msg == "Man. P2")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 0;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);

      memcpy(btCmd, h.setWriteNewPower, 6);
      btCmd[4] = 2;
      btCmd[5] = 2;
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
    else if (msg == "Man. P3")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 0;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);

      memcpy(btCmd, h.setWriteNewPower, 6);
      btCmd[4] = 2;
      btCmd[5] = 3;
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
    else if (msg == "Man. P4")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 0;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);

      memcpy(btCmd, h.setWriteNewPower, 6);
      btCmd[4] = 2;
      btCmd[5] = 4;
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
    else if (msg == "Man. P5")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 0;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);

      memcpy(btCmd, h.setWriteNewPower, 6);
      btCmd[4] = 2;
      btCmd[5] = 5;
      queueBtCommand(Helper::SET_WRITE_NEW_POWER, btCmd);
    }
  }
  else if (topic == "edilkamin/322707E4/target_temperature/set")
  {
    msg.replace(".", ""); // 24.5 to 245
    uint16_t t = msg.toInt();
    memcpy(btCmd, h.setWriteNewTemperature, 6);
    btCmd[4] = (uint8_t)(t >> 8);
    btCmd[5] = (uint8_t)t;
    queueBtCommand(Helper::SET_WRITE_NEW_TEMP, btCmd);
  }
  else if (topic == "edilkamin/322707E4/relax/set")
  {
    memcpy(btCmd, h.setRelaxStatus, 6);
    if (msg == "ON")
    {
      btCmd[4] = 1;
      btCmd[5] = 1;
    }
    else if (msg == "OFF")
    {
      btCmd[4] = 0;
      btCmd[5] = 0;
    }
    queueBtCommand(Helper::SET_RELAX_STATUS, btCmd);
  }
  else if (topic == "edilkamin/322707E4/airkare/set")
  {
  }
  else if (topic == "edilkamin/322707E4/chrono_mode/set")
  {
    memcpy(btCmd, h.setOnOffChrono, 6);
    if (msg == "ON")
    {
      btCmd[5] = 1;
    }
    else if (msg == "OFF")
    {
      btCmd[5] = 0;
    }
    queueBtCommand(Helper::SET_ON_OFF_CHRONO, btCmd);
  }
  else if (topic == "edilkamin/322707E4/standby/set")
  {
    memcpy(btCmd, h.setStandbyStatus, 6);
    if (msg == "ON")
    {
      btCmd[4] = 1;
      btCmd[5] = 1;
    }
    else if (msg == "OFF")
    {
      btCmd[4] = 0;
      btCmd[5] = 0;
    }
    queueBtCommand(Helper::SET_STANDBY_STATUS, btCmd);
  }
}

// MQTT reconnect
void mqttReconnect()
{
  // Loop until we're reconnected
  // while (!mqttClient.connected())
  //{

  Serial.print(F("Attempting MQTT connection..."));
  if (mqttClient.connect(hostname, mqttUser, mqttPass))
  {
    Serial.println(F("connected"));
    mqttClient.subscribe("edilkamin/322707E4/fan_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/hvac_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/preset_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/target_temperature/set", true);
    mqttClient.subscribe("edilkamin/322707E4/relax/set", true);
    // mqttClient.subscribe("edilkamin/322707E4/airkare/set");
    mqttClient.subscribe("edilkamin/322707E4/chrono_mode/set", true);
    mqttClient.subscribe("edilkamin/322707E4/standby/set", true);
    mqttClient.subscribe("edilkamin/322707E4/bluetooth/set", true);
    Serial.println("publishing Autodiscover Config to HA...");
    mqttClient.publish("homeassistant/climate/edilkamin_322707E4/config", h.jsonAutodiscover, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_relax/config", h.jsonAutodiscoverRelax, true);
    // mqttClient.publish("homeassistant/switch/edilkamin_322707E4_airkare/config", h.jsonAutodiscoverAirkare);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_chrono_mode/config", h.jsonAutodiscoverCronoMode, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_standby/config", h.jsonAutodiscoverStandbyMode, true);
    mqttClient.publish("homeassistant/sensor/edilkamin_322707E4_status/config", h.jsonAutodiscoverStatus, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_bluetooth/config", h.jsonAutodiscoverBluetooth, true);
    mqttClient.publish("homeassistant/sensor/edilkamin_322707E4_thermocouple_temp/config", h.jsonAutodiscoverThermocouple, true);
  }
  else
  {
    Serial.print(F("failed, rc="));
    Serial.print(mqttClient.state());
  }
  //}
}

static void bleNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  btResponse = true;
  btData = pData;
}

class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
  }

  void onDisconnect(BLEClient *pclient)
  {
    bleConnected = false;
    Serial.println(F("BLE disconnected"));
  }
};

bool connectToServer()
{
  Serial.print(F("Forming a connection to "));
  Serial.println(myDevice->getAddress().toString().c_str());

  pClient = BLEDevice::createClient();
  Serial.println(F(" - Created client"));

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(F(" - Connected to server"));

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print(F("Failed to find our service UUID: "));
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(F(" - Found our service"));

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristicWrite = pRemoteService->getCharacteristic(charUUIDWrite);
  pRemoteCharacteristicRead = pRemoteService->getCharacteristic(charUUIDRead);
  if (pRemoteCharacteristicWrite == nullptr || pRemoteCharacteristicRead == nullptr)
  {
    Serial.print(F("Failed to find our characteristic UUID: "));
    // Serial.println(charUUIDWrite.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(F(" - Found our characteristic"));

  if (pRemoteCharacteristicWrite->canNotify())
  {
    pRemoteCharacteristicRead->subscribe(true, bleNotifyCallback, false);
  }

  if (pRemoteCharacteristicRead->canNotify())
  {
    pRemoteCharacteristicRead->subscribe(true, bleNotifyCallback, false);
  }

  bleConnected = true;
  Serial.println(F("Publishing Online-state to HA..."));
  mqttClient.publish("edilkamin/322707E4/availability/state", "ONLINE", true);
  return true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice *advertisedDevice)
  {
    Serial.print(F("BLE Advertised Device found: "));
    Serial.println(advertisedDevice->toString().c_str());

    if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID))
    {
      BLEDevice::getScan()->stop();
      myDevice = advertisedDevice; /** Just save the reference now, no need to copy the object */
      bleDoConnect = true;
      bleDoScan = true;
    }
  }
};

void initWiFi()
{
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, password);
  Serial.print(F("Connecting to WiFi .."));
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.print(WiFi.localIP());
  Serial.print(", ");
  Serial.println(WiFi.getHostname());
}

void setup()
{
  Serial.begin(9600);
  initWiFi();
  h.getNtpTime();
  mqttClient.setServer(mqttHost, 1883);
  mqttClient.setBufferSize(2048);
  mqttClient.setCallback(mqttCallback);
  BLEDevice::init("");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop()
{
  currentMillis = millis();
  if (!mqttClient.loop() || !mqttClient.connected())
  {
    mqttReconnect();
    if (currentMillis - previousMillisConnect >= connectTimeout)
    {
      previousMillisConnect = currentMillis;
      ESP.restart();
    }
  }
  else {
    previousMillisConnect = currentMillis;
  }

  if (bleDoConnect == true)
  {
    if (connectToServer())
    {
      Serial.println(F("We are now connected to the BLE Server."));
      mqttClient.publish("edilkamin/322707E4/bluetooth/state", "ON");
    }
    else
      Serial.println(F("We have failed to connect to the server; there is nothin more we will do."));
    bleDoConnect = false;
  }

  if (!doBtConnect && bleConnected)
  {
    if (pClient->isConnected())
    {
      pClient->disconnect();
      mqttClient.publish("edilkamin/322707E4/availability/state", "OFFLINE", true);
      mqttClient.publish("edilkamin/322707E4/bluetooth/state", "OFF", true);
    }
  }
  else if (!bleConnected && doBtConnect)
  {
    BLEDevice::getScan()->start(0);
    mqttClient.publish("edilkamin/322707E4/bluetooth/state", "ON", true);
    status = START;
  }

  if (bleConnected)
  {
    switch (status)
    {
    case START:
      status = CHECK_BT_WRITE_QUEUE;
      break;
    case DO_NEXT_QUERY:
      if (currentMillis - previousMillis >= queryInterval)
      {
        previousMillis = currentMillis;
        nextQuery();
        status = CHECK_BT_WRITE_QUEUE;
      }
      break;
    case CHECK_BT_WRITE_QUEUE:
      if (writeQueueHasElements())
        status = BT_WRITE_REQUEST;
      else
        status = DO_NEXT_QUERY;
      break;
    case BT_WRITE_REQUEST:
      btResponse = false;
      writeBtData();
      writeTimestamp = currentMillis;
      status = AWAIT_BT_RESPONSE;
      break;
    case AWAIT_BT_RESPONSE:
      if (!btResponse && currentMillis - writeTimestamp >= responseTimeout)
      {
        Serial.println(F("Bluetooth Response Timeout !"));
        status = START;
      }
      if (btResponse)
        status = PROCESSING_BT_RESPONSE;
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
  else if (bleDoScan && doBtConnect)
  {
    mqttClient.publish("edilkamin/322707E4/availability/state", "OFFLINE", true);
    BLEDevice::getScan()->start(0);
  }
}
