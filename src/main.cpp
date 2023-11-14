#include <Arduino.h>
#include <Helper.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <TelnetStream.h>

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

enum states
{
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
const long responseTimeout = 1000;
const long connectTimeout = 1000 * 30; // 30 sec
const long bleConnectTimeout = 1000 * 300; // 5 min
const long bleOnTime = 1000 * 30; // 30 sec 
const long bleOffTime = 1000 * 120; // 2 min

byte currentFan1Level = 0;
byte currentPowerLevel = 0;
bool automatic = false;

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

void hexDebug(byte *data, size_t length)
{
  char output[(length * 2) + 1];
  char *ptr = &output[0];
  int i;
  for (i = 0; i < length; i++)
  {
    ptr += sprintf(ptr, "%02X", (int)data[i]);
  }
  debug(output);
}

void queueBtCommand(Helper::btCmds name, byte *cmd)
{
  for (uint8_t i = 0; i < btWriteQueueLength; i++)
  {
    if (btWriteQueue[i].name == Helper::NO_CMD)
    {
      btWriteQueue[i].name = name;
      memcpy(btWriteQueue[i].cmd, cmd, 6);
      debug(F(", queued at index:"));
      debug(i);
      debugln();
      break;
    }
  }
}

void nextQuery()
{
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
  for (uint8_t i = 0; i < btWriteQueueLength; i++)
  {
    if (btWriteQueue[i].name != Helper::NO_CMD)
    {
      currentOp = btWriteQueue[i].name;
      debug(F("-> Write, queue-index:"));
      byte btPacket[32];
      debug(i);
      debug(F(", "));
      debug(F("cmd:"));
      debug(currentOp);
      debug(", data:");
      h.createBtPacket(btWriteQueue[i].cmd, 6, btPacket);
      hexDebug(btWriteQueue[i].cmd, 6);
      debugln();
      btWriteQueue[i].name = Helper::NO_CMD;
      pRemoteCharacteristicWrite->writeValue(btPacket, 32);
      break;
    }
  }
}
void processBtResponseData(byte *btData)
{
  debug(F("<- Read, data:"));
  Helper::structDatagram d;
  h.getBtContent(btData, 32, &d);
  hexDebug(d.payload, 6);
  debugln();
  if (d.payload[1] == 6)
  { // Set Response
    debug(F("Set-Response for cmd:"));
    debug(currentOp);
    debug("; data:");
    hexDebug(d.payload, 6);
    debugln("");
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
    else if (currentOp == Helper::SET_WRITE_NEW_POWER && !automatic)
    {
      currentFan1Level = d.payload[4];
      currentPowerLevel = d.payload[5];
      switch (d.payload[5])
      {
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
    }
    else if (currentOp == Helper::SET_FAN_1)
    {
      currentFan1Level = d.payload[4];
      currentPowerLevel = d.payload[5];
      switch (d.payload[4])
      {
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
      {
        automatic = false;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "OFF");
      }
      else if (d.payload[4] == 1)
      {
        automatic = true;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "ON");
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
    debug(F("Response for query-cmd:"));
    debug(currentOp);
    debug("; data:");
    hexDebug(d.payload, 6);

    debugln();
    if (currentOp == Helper::READ_AUTOMATIC)
    {
      if (d.payload[3] == 1)
      {
        automatic = true;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "ON");
        mqttClient.publish("edilkamin/322707E4/preset_mode/state", "Auto");
      }
      else if (d.payload[3] == 0)
      {
        automatic = false;
        mqttClient.publish("edilkamin/322707E4/automatic_mode/state", "OFF");
      }
    }
    else if (currentOp == Helper::READ_POWER && !automatic)
    {
      currentPowerLevel = d.payload[4];
      switch (d.payload[4])
      {
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
      r = (r * 0.1) + 0.5;
      char msg_out[10];
      itoa(r, msg_out, 10);
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
      currentFan1Level = d.payload[3];
      switch (d.payload[3])
      {
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
    else if (currentOp == Helper::READ_WARNING_FLAGS)
    {
      if (d.payload[4] == 0) // Pellet ok
        mqttClient.publish("edilkamin/322707E4/pellet_level/state", "Ok");
      else if (d.payload[4] == 4) // Pellet low
        mqttClient.publish("edilkamin/322707E4/pellet_level/state", "Empty");
    }
  }
}

// MQTT-Callback
void mqttCallback(String topic, byte *message, unsigned int length)
{
  debugln();
  debug(F("MQTT-Message arrived with topic: "));
  debug(topic);
  debug(F(". Message: "));
  String msg;
  for (unsigned int i = 0; i < length; i++)
  {
    // debug((char)message[i]);
    msg += (char)message[i];
  }
  debug(msg);
  // debugln("");
  byte btCmd[6];
  if (topic == "edilkamin/322707E4/bluetooth/set")
  {
    if (msg == "ON")
      doBtConnect = true;
    if (msg == "OFF")
      doBtConnect = false;
  }
  else if (topic == "edilkamin/322707E4/hvac_mode/set")
  {
    memcpy(btCmd, h.setOnOff, 6);
    if (msg == "heat")
      btCmd[5] = 1;
    else if (msg == "off")
      btCmd[5] = 0;
    queueBtCommand(Helper::SET_ON_OFF, btCmd);
  }
  else if (topic == "edilkamin/322707E4/fan_mode/set" && currentFan1Level != 0 && currentPowerLevel != 0) // ignore set command if fan & power-level is 0 (=boot)
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
  }
  else if (topic == "edilkamin/322707E4/automatic_mode/set")
  {
    byte btPacket[32];
    memcpy(btCmd, h.setChangeAutoSwitch, 6);
    if (msg == "ON")
      btCmd[4] = 1;
    else if (msg == "OFF")
      btCmd[4] = 0;
    queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);
  }
  else if (topic == "edilkamin/322707E4/preset_mode/set" && currentFan1Level != 0 && currentPowerLevel != 0) // ignore set command if fan & power-level is 0 (=boot)
  {
    byte btPacket[32];
    if (msg == "Auto")
    {
      memcpy(btCmd, h.setChangeAutoSwitch, 6);
      btCmd[4] = 1;
      queueBtCommand(Helper::SET_CHANGE_AUTO_SWITCH, btCmd);
    }
    else
    {
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
      btCmd[5] = 1;
    else if (msg == "OFF")
      btCmd[5] = 0;
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
  debug(F("Attempting MQTT connection..."));
  if (mqttClient.connect(hostname, MQTT_USER, MQTT_PASSWORD))
  {
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
    mqttClient.publish("homeassistant/climate/edilkamin_322707E4/config", h.jsonAutodiscover, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_automatic_mode/config", h.jsonAutodiscoverAutomaticMode, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_relax/config", h.jsonAutodiscoverRelax, true);
    // mqttClient.publish("homeassistant/switch/edilkamin_322707E4_airkare/config", h.jsonAutodiscoverAirkare);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_chrono_mode/config", h.jsonAutodiscoverCronoMode, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_standby/config", h.jsonAutodiscoverStandbyMode, true);
    mqttClient.publish("homeassistant/sensor/edilkamin_322707E4_status/config", h.jsonAutodiscoverStatus, true);
    mqttClient.publish("homeassistant/switch/edilkamin_322707E4_bluetooth/config", h.jsonAutodiscoverBluetooth, true);
    mqttClient.publish("homeassistant/sensor/edilkamin_322707E4_thermocouple_temp/config", h.jsonAutodiscoverThermocouple, true);
    mqttClient.publish("homeassistant/sensor/edilkamin_322707E4_pellet_level/config", h.jsonAutodiscoverPelletLevel, true);
  }
  else
  {
    debug(F("failed, rc="));
    debug(mqttClient.state());
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
    debugln();
    debugln(F("BLE disconnected..."));
  }
};

bool connectToServer()
{
  debug(F("Forming a connection to "));
  debugln(myDevice->getAddress().toString().c_str());

  pClient = BLEDevice::createClient();
  debugln(F(" - Created client"));

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  debugln(F(" - Connected to server"));

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    debug(F("Failed to find our service UUID: "));
    debugln(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  debugln(F(" - Found our service"));

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristicWrite = pRemoteService->getCharacteristic(charUUIDWrite);
  pRemoteCharacteristicRead = pRemoteService->getCharacteristic(charUUIDRead);
  if (pRemoteCharacteristicWrite == nullptr || pRemoteCharacteristicRead == nullptr)
  {
    debug(F("Failed to find our characteristic UUID: "));
    // debugln(charUUIDWrite.toString().c_str());
    pClient->disconnect();
    return false;
  }
  debugln(F(" - Found our characteristic"));

  if (pRemoteCharacteristicWrite->canNotify())
  {
    pRemoteCharacteristicRead->subscribe(true, bleNotifyCallback, false);
  }

  if (pRemoteCharacteristicRead->canNotify())
  {
    pRemoteCharacteristicRead->subscribe(true, bleNotifyCallback, false);
  }

  bleConnected = true;
  debugln(F("Publishing Online-state to HA..."));
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
    debug(F("BLE Advertised Device found: "));
    debugln(advertisedDevice->toString().c_str());

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
  WiFi.begin(WLAN_SSID, WLAN_PASSWORD);
  debug(F("Connecting to WiFi .."));
  while (WiFi.status() != WL_CONNECTED)
  {
    debug('.');
    delay(1000);
  }
  debug(WiFi.localIP());
  debug(", ");
  debugln(WiFi.getHostname());
}

void setup()
{
  Serial.begin(9600);
  initWiFi();
  h.getNtpTime();
  TelnetStream.begin();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(2048);
  mqttClient.setCallback(mqttCallback);
  msLastBleConnect = millis(); 
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

  // Telnet-Terminal
  switch (TelnetStream.read())
  {
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
  }
  else { 
      msLastBleConnect = currentMillis;
  }

  // MQTT Watchdog - Restart ESP if no connect to MQTT-Server
  if (!mqttClient.loop() || !mqttClient.connected())
  {
    mqttReconnect();
    if (currentMillis - msLastMqttConnect >= connectTimeout)
    {
      msLastMqttConnect = currentMillis;
      debugln(F("MQTT not connected watchdog triggered - ESP Restart..."));
      ESP.restart();
    }
  }
  else
  {
    msLastMqttConnect = currentMillis;
  }

  if (bleDoConnect == true)
  {
    if (connectToServer())
    {
      debugln(F("We are now connected to the BLE Server."));
      mqttClient.publish("edilkamin/322707E4/bluetooth/state", "ON");
    }
    else
      debugln(F("Failed to connect to the MQTT-Server; there is nothing more we will do."));
    bleDoConnect = false;
  }

  if (!doBtConnect && bleConnected)
  {
    if (pClient->isConnected())
    {
      pClient->disconnect();
      // mqttClient.publish("edilkamin/322707E4/availability/state", "OFFLINE", true);
      mqttClient.publish("edilkamin/322707E4/bluetooth/state", "OFF", true);
    }
  }
  else if (!bleConnected && doBtConnect)
  {
    BLEDevice::getScan()->start(5, false);
    mqttClient.publish("edilkamin/322707E4/bluetooth/state", "ON", true);
    status = START;
  }

  switch (status)
  {
  case START:
    status = CHECK_BT_WRITE_QUEUE;
    break;
  case DO_NEXT_QUERY:
    if (currentMillis - msLastQuery >= queryInterval)
    {
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
    if (bleConnected && currentMillis - bleLastConnect >= bleOnTime) 
    {
      doBtConnect = false;
      bleLastSwitchOff = currentMillis;
      status = START;
    }
    else if (bleConnected)
    {
      status = DO_NEXT_QUERY;
    }
    if (!bleConnected && currentMillis - bleLastSwitchOff >= bleOffTime)
    {
      debugln(F("Bluetooth wake up..."));
      status = DO_NEXT_QUERY;
    }  
    else if (!bleConnected)  {
      status = START;
    }
    break;
  case BT_CONNECT_CHECK:
    if (bleConnected)
      status = BT_WRITE_REQUEST;
    else
    {
      status = BT_CONNECT_CHECK;
      doBtConnect = true;
      bleLastConnect = currentMillis;
    }
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
      debugln(F("Bluetooth Response Timeout !"));
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

