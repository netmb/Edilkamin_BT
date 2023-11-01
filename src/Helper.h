// helper.h
#ifndef Helper_h
#define Helper_h

#include <Arduino.h>

class Helper
{
private:
public:
  enum btCmds
  {
    NO_CMD,
    READ_BOILER_SET,
    READ_STOVE_TYPE,
    READ_BOILER_TEMP,
    READ_THERMOCOUPLE_TEMP,
    READ_PUFFER_TEMP,
    READ_SANI_BOILER_BOTTOM_TEMP,
    READ_BOILER_BOTTOM_TEMP,
    READ_DATE_TIME,
    READ_WEB_TIME,
    READ_EASY_TIMER_TIME,
    READ_ALARM,
    READ_TOTAL_ALARMS,
    READ_ALARM_LOGS_1,
    READ_ALARM_LOGS_2,
    READ_ALARM_LOGS_3,
    READ_ALARM_LOGS_4,
    READ_AUTOMATIC,
    READ_TEMPERATURE,
    READ_POWER,
    READ_FUMES_TEMP_OFF,
    READ_FAN_MACHINE,
    READ_FAN_23_MACHINE,
    READ_FAN,
    READ_FAN_23,
    READ_RELAX,
    READ_FIREPLACE_COMMANDS,
    READ_FIREPLACE_STOVE_STATUS,
    READ_FIREPLACE_CURRENT_POWER,
    READ_PELLET_SENSOR,
    READ_WARNING_FLAGS,
    READ_PELLET_REMAINING,
    READ_NUMBER_FAN,
    READ_FAN_1_2_ENV,
    READ_FAN_3_ENV,
    READ_STATS,
    READ_MAIN_ENV_TEMP,
    READ_CANALIZED_2,
    READ_TEMP_CANALIZED_2,
    READ_TEMP_CANALIZED_3,
    READ_CANALIZED_3,
    READ_DATA,
    READ_STANDBY_STATUS,
    READ_STANDBY_START_TIME,
    READ_STANDBY_TIMER,
    READ_MOTHERBOARD_SERIAL,
    READ_MOTHERBOARD_NAME,
    READ_MOTHERBOARD_BOOTLOADER,
    READ_MOTHERBOARD_BL_VERSION,
    READ_MOTHERBOARD_APP,
    READ_MOTHERBOARD_APP_VERSION,
    READ_REMOTE_CONTROL_SERIAL,
    READ_REMOTE_CONTROL_NAME,
    READ_DISPLAY_IDRO,
    READ_EMERGENCY_PANEL_VERSION,
    READ_COMAND_VERSION,
    READ_COMAND_EMERGENCY_VERSION,
    READ_TEMP_FORMAT,
    READ_CHRONO_DATA,
    READ_CHRONO_POWER_DATA,
    READ_ON_OFF_CHRONO,
    READ_ECO_TEMP,
    READ_COMFORT_TEMP,
    READ_FIREPLACE_MAIN_STATUS,
    SET_TEMP_ECO,
    SET_TEMP_COMFORT,
    SET_EASY_TIME,
    SET_ON_OFF_CHRONO,
    SET_RELAX_STATUS,
    SET_FAN_1,
    SET_FAN_2,
    SET_FAN_3,
    SET_CANALIZED_TEMP,
    SET_AIRKARE,
    SET_CONTINUE_COCLEA,
    SET_BLE_REGISTER,
    SET_CHARGE_COCLEA,
    SET_STANDBY_TIME,
    SET_SWEEP,
    SET_TEMP_FORMAT,
    SET_STANDBY_STATUS,
    SET_WRITE_NEW_TEMP,
    SET_WRITE_NEW_POWER,
    SET_CHANGE_AUTO_SWITCH,
    SET_ON_OFF
  };

  struct structDatagram
  {
    byte timestamp[4] = {0x00, 0x00, 0x00, 0x00};
    byte fixedKey[16] = {0x31, 0xdd, 0x34, 0x51, 0x26, 0x39, 0x20, 0x23, 0x9f, 0x4b, 0x68, 0x20, 0xe7, 0x25, 0xfc, 0x75};
    byte payload[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    byte checksum[2] = {0x00, 0x00};
    byte padding[4] = {0x04, 0x04, 0x04, 0x04};
  };

  const byte readBoilerSet[6] PROGMEM = {1, 3, 2, 238, 0, 1};
  const byte readStoveType[6] PROGMEM = {1, 3, 5, 221, 0, 1};
  const byte readBoilerTemperature[6] PROGMEM = {1, 3, 2, 181, 0, 1};
  const byte readThermocoupleTemperature[6] PROGMEM = {1, 3, 2, 187, 0, 1};
  const byte readPufferTemperature[6] PROGMEM = {1, 3, 2, 182, 0, 1};
  const byte readSaniBoilerTemperature[6] PROGMEM = {1, 3, 2, 184, 0, 1};
  const byte readBoilerBottomTemperature[6] PROGMEM = {1, 3, 2, 186, 0, 1};
  const byte readDateTime[6] PROGMEM = {1, 3, 3, 22, 0, 4};
  const byte readWebTime[6] PROGMEM = {1, 3, 5, 56, 0, 1};
  const byte readEasyTimerTime[6] PROGMEM = {1, 3, 3, 27, 0, 1};
  const byte readAlarm[6] PROGMEM = {1, 3, 2, 230, 0, 1};
  const byte readTotalAlarms[6] PROGMEM = {1, 3, 3, 58, 0, 1};
  const byte readAlarmLogs1[6] PROGMEM = {1, 3, 3, 60, 0, 100};
  const byte readAlarmLogs2[6] PROGMEM = {1, 3, 3, 160, 0, 100};
  const byte readAlarmLogs3[6] PROGMEM = {1, 3, 4, 4, 0, 100};
  const byte readAlarmLogs4[6] PROGMEM = {1, 3, 4, 104, 0, 100};
  const byte readAutomatic[6] PROGMEM = {1, 3, 5, 35, 0, 1};
  const byte readTemperature[6] PROGMEM = {1, 3, 5, 37, 0, 1};
  const byte readPower[6] PROGMEM = {1, 3, 5, 41, 0, 1};
  const byte readFumesTemperatureOff[6] PROGMEM = {1, 3, 5, 168, 0, 1};
  const byte readFanMachine[6] PROGMEM = {1, 3, 2, 231, 0, 1};
  const byte readFan23Machine[6] PROGMEM = {1, 3, 2, 232, 0, 1};
  const byte readFan[6] PROGMEM = {1, 3, 5, 41, 0, 1};
  const byte readFan23[6] PROGMEM = {1, 3, 5, 42, 0, 1};
  const byte readRelax[6] PROGMEM = {1, 3, 5, 43, 0, 1};
  const byte readFireplaceCommands[6] PROGMEM = {1, 3, 3, 28, 0, 1};
  const byte readFireplaceStoveStatus[6] PROGMEM = {1, 3, 2, 229, 0, 1};
  const byte readFireplaceCurrentPower[6] PROGMEM = {1, 3, 2, 231, 0, 1};
  const byte readPelletSensor[6] PROGMEM = {1, 3, 5, 46, 0, 1};
  const byte readWarningFlags[6] PROGMEM = {1, 3, 2, 244, 0, 2};
  const byte readPelletRemaining[6] PROGMEM = {1, 3, 2, 236, 0, 1};
  const byte readNumberFan[6] PROGMEM = {1, 3, 5, 72, 0, 1};
  const byte readFan1_2Env[6] PROGMEM = {1, 3, 5, 73, 0, 1};
  const byte readFan3Env[6] PROGMEM = {1, 3, 5, 74, 0, 1};
  const byte readStats[6] PROGMEM = {1, 3, 2, 194, 0, 1};
  const byte readMainEnvTemp[6] PROGMEM = {1, 3, 2, 233, 0, 1};
  const byte readCanalized2[6] PROGMEM = {1, 3, 2, 234, 0, 1};
  const byte readTempCanalized2[6] PROGMEM = {1, 3, 5, 38, 0, 1};
  const byte readTempCanalized3[6] PROGMEM = {1, 3, 2, 39, 0, 1};
  const byte readCanalized3[6] PROGMEM = {1, 3, 2, 235, 0, 1};
  const byte readData[6] PROGMEM = {1, 3, 3, 52, 0, 6};
  const byte readStandbyStatus[6] PROGMEM = {1, 3, 5, 44, 0, 1};
  const byte readStandbyStartTime[6] PROGMEM = {1, 3, 2, 252, 0, 1};
  const byte readStandbyTimer[6] PROGMEM = {1, 3, 5, 45, 0, 1};
  const byte readMotherboardSerial[6] PROGMEM = {1, 3, 0, 10, 0, 8};
  const byte readMotherboardName[6] PROGMEM = {1, 3, 0, 18, 0, 8};
  const byte readMotherboardBootloader[6] PROGMEM = {1, 3, 0, 26, 0, 8};
  const byte readMotherboardBLVersion[6] PROGMEM = {1, 3, 0, 34, 0, 8};
  const byte readMotherboardApplication[6] PROGMEM = {1, 3, 0, 42, 0, 8};
  const byte readMotherboardAppVersion[6] PROGMEM = {1, 3, 0, 50, 0, 8};
  const byte readRemoteControlSerial[6] PROGMEM = {1, 3, 0, 60, 0, 8};
  const byte readRemoteControlName[6] PROGMEM = {1, 3, 0, 118, 0, 8};
  const byte readDisplayIdro[6] PROGMEM = {1, 3, 0, 150, 0, 8};
  const byte readEmergencyPannelVersion[6] PROGMEM = {1, 3, 0, 100, 0, 8};
  const byte readComandVersion[6] PROGMEM = {1, 3, 0, 150, 0, 8};
  const byte readComandEmergencyVersion[6] PROGMEM = {1, 3, 2, 38, 0, 8};
  const byte readTemperatureFormat[6] PROGMEM = {1, 3, 5, 36, 0, 1};
  const byte readChronoData[6] PROGMEM = {1, 3, 4, 206, 0, 42};
  const byte readChronoPowerData[6] PROGMEM = {1, 3, 4, 248, 0, 42};
  const byte readOnOffChrono[6] PROGMEM = {1, 3, 5, 34, 0, 1};
  const byte readEconomyTemperature[6] PROGMEM = {1, 3, 4, 204, 0, 1};
  const byte readComfortTemperature[6] PROGMEM = {1, 3, 4, 205, 0, 1};
  const byte readFireplaceMainStatus[6] PROGMEM = {1, 3, 2, 228, 0, 1};
  // set
  const byte setTemperatureEconomy[6] PROGMEM = {1, 6, 4, 204, 0, 2};  // B5: 0,1; B6:2,3
  const byte setTemperatureComfort[6] PROGMEM = {1, 6, 4, 205, 0, 2};  // B5: 0,1; B6:2,3
  const byte setEasyTime[6] PROGMEM = {1, 6, 3, 27, 0, 2};             // B5: 0,1; B6:2,3
  const byte setOnOffChrono[6] PROGMEM = {1, 6, 5, 34, 0, 0};          // B5 = ChronoProgram; B6: 0,1 = on,off
  const byte setRelaxStatus[6] PROGMEM = {1, 6, 5, 43, 0, 0};          // B5 = 0,1 =off,on; B6 = 0,1 =off, on; off=00 on=11
  const byte setFan1[6] PROGMEM = {1, 6, 5, 41, 0, 0};                 // B5: Power 0-6; B6:PowerLevel (notFan)
  const byte setFan2[6] PROGMEM = {1, 6, 5, 42, 0, 0};                 // B5: ?; B6:0,1 = Powerstate?
  const byte setFan3[6] PROGMEM = {1, 6, 5, 43, 0, 0};                 // B5: ?; B6:0,1 = Powerstate?
  const byte setCanalizedTemperature[6] PROGMEM = {1, 6, 5, 38, 0, 2}; // B4: 38,39; B5: 0,1; B6:2,3
  const byte setAirKare[6] PROGMEM = {1, 6, 3, 28, 0, 0};              // B5: not set, so assume 0?; B6: not set, so assume 0?
  const byte setContinueCoclea[6] PROGMEM = {1, 6, 5, 46, 0, 0};       // B5: 0,1; B6: 0,1
  const byte setBleRegister[6] PROGMEM = {1, 6, 5, 56, 0, 0};          // B6: unknown
  const byte setChargeCoclea[6] PROGMEM = {1, 6, 3, 28, 0, 0};         // B5: ?, B6: 0,1
  const byte setStandbyTime[6] PROGMEM = {1, 6, 5, 45, 0, 0};          // B5: 0,1; B6: 0,1
  const byte setSweep[6] PROGMEM = {1, 6, 3, 28, 0, 0};
  const byte setTemperatureFormat[6] PROGMEM = {1, 6, 5, 36, 0, 0};   // B5: ?; B6: 0,1
  const byte setStandbyStatus[6] PROGMEM = {1, 6, 5, 44, 0, 0};       // B5: 0,1
  const byte setWriteNewTemperature[6] PROGMEM = {1, 6, 5, 37, 0, 0}; // B4: 37,38,39; B5: ?; B6: ?
  const byte setWriteNewPower[6] PROGMEM = {1, 6, 5, 41, 0, 0};       // B5=fan1Value, B6= PowerLevel (not Fan)
  const byte setChangeAutoSwitch[6] PROGMEM = {1, 6, 5, 35, 1, 1};    // B5= 0,1 off,on
  const byte setOnOff[6] PROGMEM = {1, 6, 3, 28, 0, 0};               // B5 = 0, B6 = 1 = on, 0= 0ff

  const char *jsonAutodiscover PROGMEM = "{\"name\": \"Edilkamin\",\"unique_id\": \"322707E4_edilkamin_climate\",\"icon\": \"mdi:gas-burner\", \"availability\": {\"topic\": \"edilkamin/322707E4/availability/state\",\"payload_available\": \"ONLINE\",\"payload_not_available\": \"OFFLINE\"},\"fan_mode_command_topic\": \"edilkamin/322707E4/fan_mode/set\",\"fan_mode_state_topic\": \"edilkamin/322707E4/fan_mode/state\",\"fan_modes\": [\"Auto\",\"20%\",\"40%\",\"60%\",\"80%\",\"100%\"],\"mode_command_topic\": \"edilkamin/322707E4/hvac_mode/set\",\"mode_state_topic\": \"edilkamin/322707E4/hvac_mode/state\",\"modes\": [\"heat\",\"off\"],\"preset_mode_command_topic\": \"edilkamin/322707E4/preset_mode/set\",\"preset_mode_state_topic\": \"edilkamin/322707E4/preset_mode/state\",\"preset_modes\": [\"Auto\",\"Man. P1\",\"Man. P2\",\"Man. P3\",\"Man. P4\",\"Man. P5\"],\"min_temp\": 20,\"max_temp\": 30,\"precision\": 0.5,\"retain\": true,\"temperature_command_topic\": \"edilkamin/322707E4/target_temperature/set\",\"temperature_state_topic\": \"edilkamin/322707E4/target_temperature/state\",\"current_temperature_topic\": \"edilkamin/322707E4/temperature/state\",\"temp_step\": 0.5,\"device\": {\"manufacturer\": \"Edilkamin\",\"identifiers\": [\"322707E4\"],\"name\": \"Edilkamin\"}}";
  const char *jsonAutodiscoverRelax PROGMEM = "{\"name\": \"Relax\",\"unique_id\": \"322707E4_edilkamin_relax\",\"icon\": \"mdi:volume-off\", \"device\": {\"identifiers\": [\"322707E4\"] },\"availability\": {\"topic\": \"edilkamin/322707E4/availability/state\",\"payload_available\": \"ONLINE\",\"payload_not_available\": \"OFFLINE\"},\"command_topic\": \"edilkamin/322707E4/relax/set\",\"state_topic\": \"edilkamin/322707E4/relax/state\", \"retain\": true}";
  // const char *jsonAutodiscoverAirkare PROGMEM = "{\"name\": \"Airkare\",\"unique_id\": \"322707E4_edilkamin_airkare\",\"device\": {\"identifiers\": [\"322707E4\"] },\"availability\": {\"topic\": \"edilkamin/322707E4/availability/state\",\"payload_available\": \"ONLINE\",\"payload_not_available\": \"OFFLINE\"},\"command_topic\": \"edilkamin/322707E4/airkare/set\",\"state_topic\": \"edilkamin/322707E4/airkare/state\", \"retain\": true}";
  const char *jsonAutodiscoverCronoMode PROGMEM = "{\"name\": \"Chrono\",\"unique_id\": \"322707E4_edilkamin_chrono_mode\",\"icon\": \"mdi:clock-outline\", \"device\": {\"identifiers\": [\"322707E4\"] },\"availability\": {\"topic\": \"edilkamin/322707E4/availability/state\",\"payload_available\": \"ONLINE\",\"payload_not_available\": \"OFFLINE\"},\"command_topic\": \"edilkamin/322707E4/chrono_mode/set\",\"state_topic\": \"edilkamin/322707E4/chrono_mode/state\", \"retain\": true}";
  const char *jsonAutodiscoverStandbyMode PROGMEM = "{\"name\": \"Standby\",\"unique_id\": \"322707E4_edilkamin_standby\",\"icon\": \"mdi:sleep\", \"device\": {\"identifiers\": [\"322707E4\"] },\"availability\": {\"topic\": \"edilkamin/322707E4/availability/state\",\"payload_available\": \"ONLINE\",\"payload_not_available\": \"OFFLINE\"},\"command_topic\": \"edilkamin/322707E4/standby/set\",\"state_topic\": \"edilkamin/322707E4/standby/state\", \"retain\": true}";
  const char *jsonAutodiscoverStatus PROGMEM = "{\"name\":\"Status\",\"unique_id\":\"322707E4_edilkamin_status\",\"icon\": \"mdi:state-machine\", \"device\":{ \"identifiers\":[ \"322707E4\" ]},\"availability\":{ \"topic\":\"edilkamin/322707E4/availability/state\", \"payload_available\":\"ONLINE\", \"payload_not_available\":\"OFFLINE\"},\"state_topic\":\"edilkamin/322707E4/status/state\"}";
  const char *jsonAutodiscoverBluetooth PROGMEM = "{\"name\": \"Bluetooth connect\",\"unique_id\": \"322707E4_edilkamin_bluetooth\",\"icon\": \"mdi:bluetooth\", \"device\": {\"identifiers\": [\"322707E4\"]},\"command_topic\": \"edilkamin/322707E4/bluetooth/set\",\"state_topic\": \"edilkamin/322707E4/bluetooth/state\",\"retain\": true}";
  const char *jsonAutodiscoverThermocouple PROGMEM = "{\"name\": \"Exhaust Temperature\",\"unique_id\": \"322707E4_edilkamin_thermocouple_temperature\",\"device_class\": \"temperature\",\"device\": {\"identifiers\": [\"322707E4\"]},\"availability\": {\"topic\": \"edilkamin/322707E4/availability/state\",\"payload_available\": \"ONLINE\",\"payload_not_available\": \"OFFLINE\"},\"state_topic\": \"edilkamin/322707E4/thermocouple_temperature/state\"}";
  const char *jsonAutodiscoverPelletLevel PROGMEM = "{\"name\":\"Pellet Level\",\"unique_id\":\"322707E4_edilkamin_pellet_level\",\"icon\": \"mdi:grain\", \"device\":{ \"identifiers\":[ \"322707E4\" ]},\"availability\":{ \"topic\":\"edilkamin/322707E4/availability/state\", \"payload_available\":\"ONLINE\", \"payload_not_available\":\"OFFLINE\"},\"state_topic\":\"edilkamin/322707E4/pellet_level/state\"}";
  const char *jsonAutodiscoverAutomaticMode PROGMEM = "{\"name\": \"Automatic Mode\",\"unique_id\": \"322707E4_edilkamin_automatic_mode\",\"icon\": \"mdi:refresh-auto\",\"device\": {   \"identifiers\": [  \"322707E4\"   ]},\"availability\": {   \"topic\": \"edilkamin/322707E4/availability/state\",   \"payload_available\": \"ONLINE\",   \"payload_not_available\": \"OFFLINE\"},\"command_topic\": \"edilkamin/322707E4/automatic_mode/set\",\"state_topic\": \"edilkamin/322707E4/automatic_mode/state\",\"retain\": true }";

  void aesEncrypt(char *plainText, byte *output);
  void aesDecrypt(byte *cryptedText, byte *output);
  void getNtpTime();
  void getTimestamp(byte *ts);
  void hexDebug(byte *data, size_t length);
  void crc16modbus(byte *msg, size_t len, byte *crcOut);
  void createBtPacket(byte *cmd, size_t len, byte *btPacket);
  void getBtContent(byte *rcvd, size_t len, structDatagram *d);
};

#endif