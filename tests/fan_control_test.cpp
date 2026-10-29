#include "FanControl.h"
#include "ModbusResponse.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

static FanControl threeFans() {
  FanControl fans;
  fans.setEquipment(3);
  fans.setRelax(false);
  assert(fans.updatePair(2, 2));
  return fans;
}

static void expectCommand(const uint8_t actual[6], uint8_t fan3, uint8_t fan2) {
  const uint8_t expected[] = {1, 6, 5, 42, fan3, fan2};
  assert(memcmp(actual, expected, 6) == 0);
}

static std::vector<uint8_t> frame(std::initializer_list<uint8_t> data) {
  std::vector<uint8_t> result(data);
  const uint16_t crc = Modbus::crc16(result.data(), result.size());
  result.push_back(crc & 0xff);
  result.push_back(crc >> 8);
  return result;
}

int main() {
  const bool supported[5][3] = {
      {false, false, false}, {true, false, false}, {true, true, false},
      {true, true, true}, {false, true, true}};
  for (uint8_t code = 0; code <= 4; ++code)
    for (uint8_t fan = 1; fan <= 3; ++fan)
      assert(FanControl::supports(code, fan) == supported[code][fan - 1]);
  assert(!FanControl::supports(5, 2));
  assert(!FanControl::supports(255, 2));
  assert(!FanControl::supports(3, 0));
  assert(!FanControl::supports(4, 4));
  assert(FanControl::parseMode("20%") == 1);
  assert(FanControl::parseMode("100%") == 5);
  assert(FanControl::parseMode("Auto") == 6);
  for (const char *bad : {"0", "OFF", "", "bogus", "120%", "auto", "20% junk"})
    assert(FanControl::parseMode(bad) == 0);

  uint8_t cmd[6] = {};
  FanControl cold;
  assert(!cold.request(2, 3));
  cold.setEquipment(3);
  assert(!cold.request(2, 3)); // Relax state is still unknown.
  cold.setRelax(false);
  assert(cold.request(2, 3));
  assert(!cold.beginWrite(cmd)); // The partner byte has not been read.

  auto fans = threeFans();
  assert(!fans.request(2, 0));
  assert(!fans.request(2, 7));
  assert(!fans.request(1, 3));
  assert(fans.request(2, 3));
  assert(fans.request(3, 4));
  assert(fans.beginWrite(cmd));
  expectCommand(cmd, 4, 3); // Both rapid changes survive in the same register.
  assert(!fans.beginWrite(cmd));
  assert(fans.request(2, 5)); // Command arrives while previous write is in flight.
  assert(!fans.beginWrite(cmd));
  assert(fans.updatePair(4, 3)); // Echo of the earlier write.
  assert(fans.beginWrite(cmd));
  expectCommand(cmd, 4, 5); // Pending newer request was not overwritten by echo.
  assert(fans.updatePair(4, 5));
  assert(fans.request(3, 1));
  assert(fans.request(3, 6));
  assert(fans.beginWrite(cmd));
  expectCommand(cmd, 6, 5); // Latest unsent request wins.

  fans.failWrite();
  assert(!fans.pairValid());
  assert(!fans.pending());
  assert(fans.request(2, 3));
  assert(!fans.beginWrite(cmd)); // Do not guess the state after a lost echo.
  assert(fans.updatePair(1, 2));
  assert(fans.beginWrite(cmd));
  expectCommand(cmd, 1, 3);

  fans = threeFans();
  fans.requireRefresh(); // Scheduled BLE sleep; accept and defer a new request.
  assert(fans.request(2, 4));
  assert(!fans.beginWrite(cmd));
  fans.setEquipment(3);
  assert(!fans.beginWrite(cmd));
  fans.setRelax(false);
  assert(!fans.beginWrite(cmd));
  fans.updatePair(6, 1); // Fresh value may have been changed by another client.
  assert(fans.beginWrite(cmd));
  expectCommand(cmd, 6, 4);

  fans = threeFans();
  assert(fans.request(2, 3));
  fans.setRelax(true);
  assert(!fans.pending());
  assert(!fans.canAccept(2));
  assert(!fans.beginWrite(cmd));
  fans.setRelax(false);
  fans.setEquipment(2);
  assert(!fans.request(3, 4));
  fans.updatePair(0, 2); // Missing Fan3 is a known zero, not an uninitialized byte.
  assert(fans.request(2, 5));
  assert(fans.beginWrite(cmd));
  expectCommand(cmd, 0, 5);
  fans.setEquipment(4);
  assert(!fans.hasFan(1) && fans.hasFan(2) && fans.hasFan(3));
  assert(!fans.updatePair(255, 1));
  assert(!fans.pairValid());
  fans.setEquipment(99);
  assert(!fans.equipmentKnown() && !fans.request(2, 3));

  const uint8_t read[] = {1, 3, 5, 42, 0, 1};
  assert(Modbus::crc16(read, 6) == 0x0ea5); // Literal CRC from the APK.
  const uint8_t readEquipment[] = {1, 3, 5, 72, 0, 1};
  assert(Modbus::crc16(readEquipment, 6) == 0xd004); // APK literal.
  auto response = frame({1, 3, 2, 6, 3});
  assert(Modbus::validate(response.data(), response.size(), read) == Modbus::Response::Read);
  assert(response[3] == 6 && response[4] == 3);
  assert(Modbus::validate(response.data(), 6, read) == Modbus::Response::Invalid);
  response[3] ^= 1;
  assert(Modbus::validate(response.data(), response.size(), read) == Modbus::Response::Invalid);
  response = frame({2, 3, 2, 6, 3});
  assert(Modbus::validate(response.data(), response.size(), read) == Modbus::Response::Invalid);
  response = frame({1, 3, 4, 0, 0, 0, 0});
  assert(Modbus::validate(response.data(), response.size(), read) == Modbus::Response::Invalid);
  response = frame({1, 0x83, 2});
  assert(Modbus::validate(response.data(), response.size(), read) == Modbus::Response::Exception);

  const uint8_t write[] = {1, 6, 5, 42, 6, 3};
  response = frame({1, 6, 5, 42, 6, 3});
  assert(Modbus::validate(response.data(), response.size(), write) == Modbus::Response::Write);
  response = frame({1, 6, 5, 43, 6, 3}); // Relax-register echo must not be accepted.
  assert(Modbus::validate(response.data(), response.size(), write) == Modbus::Response::Invalid);
  response = frame({1, 6, 5, 42, 2, 3}); // Stale echo, correct register but wrong value.
  assert(Modbus::validate(response.data(), response.size(), write) == Modbus::Response::Invalid);
  response = frame({1, 3, 2, 6, 3});
  assert(Modbus::validate(response.data(), response.size(), write) == Modbus::Response::Invalid);
  const uint8_t readTwo[] = {1, 3, 2, 244, 0, 2};
  response = frame({1, 3, 4, 0, 0, 0, 4});
  assert(Modbus::validate(response.data(), response.size(), readTwo) == Modbus::Response::Read);
  puts("Fan control and Modbus response tests passed");
}
