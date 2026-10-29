#include "StoveProtocol.h"
#include "ModbusResponse.h"
#include <assert.h>
#include <initializer_list>
#include <vector>
using namespace StoveProtocol;

static std::vector<uint8_t> response(std::initializer_list<uint8_t> bytes) {
  std::vector<uint8_t> result(bytes);
  auto crc = Modbus::crc16(result.data(), result.size());
  result.push_back(crc & 0xff);
  result.push_back(crc >> 8);
  return result;
}
static void equal(const uint8_t *actual, std::initializer_list<uint8_t> expected) {
  assert(memcmp(actual, expected.begin(), expected.size()) == 0);
}
int main() {
  uint16_t t = 0;
  for (auto text : {"24", "24.0", "24.00"}) {
    assert(parseTemperature(text, t) && t == 240);
  }
  assert(parseTemperature("24.50", t) && t == 245);
  assert(parseTemperature("5", t) && t == 50);
  assert(parseTemperature("35.00", t) && t == 350);
  assert(parseTemperature("25.6", t) && t == 256);
  for (auto text : {"", "-1", "+24", "24C", "24.", "24.55", "35.1", "4.9",
                    "350", "NaN", "inf", " 24", "24 ", "24,5", "2e1",
                    "24.5.0", "99999999999999999999999999"})
    assert(!parseTemperature(text, t));
  assert(!parseTemperature(nullptr, t));
  const uint8_t temp30[] = {1, 44}, temp256[] = {1, 0};
  char text[64];
  temperature(word(temp30), text, sizeof(text));
  assert(strcmp(text, "30.0") == 0);
  temperature(word(temp256), text, sizeof(text));
  assert(strcmp(text, "25.6") == 0);
  temperature(0, text, sizeof(text));
  assert(strcmp(text, "0.0") == 0);
  for (int flags = 0; flags < 256; ++flags) {
    const uint8_t warning[] = {0xa5, uint8_t(flags), 0x80, 0x33};
    assert(pelletReserve(warning) == ((flags & 4) != 0));
  }
  for (uint8_t sub = 0; sub < 3; ++sub) {
    statusText(sub, 1, text, sizeof(text));
    assert(strcmp(text, "Cleaning") == 0);
  }
  statusText(9, 5, text, sizeof(text));
  assert(strcmp(text, "Final cleaning") == 0);
  statusText(254, 253, text, sizeof(text));
  assert(strcmp(text, "Unknown (main=253, sub=254)") == 0);

  ControlTransaction transaction;
  uint8_t command[] = {1, 6, 3, 28, 0, 1};
  uint8_t output[6] = {};
  assert(transaction.begin(command));
  equal(command, {1, 3, 3, 28, 0, 1});
  assert(!transaction.take(output)); // Cannot write before a confirmed read.
  auto read = response({1, 3, 2, 0x04, 0xa0}); // AirKare plus other low-byte bits.
  assert(Modbus::validate(read.data(), read.size(), command) == Modbus::Response::Read);
  transaction.acknowledge(read.data());
  assert(transaction.take(output));
  equal(output, {1, 6, 3, 28, 4, 0xa1});
  assert(!transaction.take(output));
  // Every new command reads again; an external change must survive OFF too.
  uint8_t off[] = {1, 6, 3, 28, 0, 0};
  assert(transaction.begin(off));
  read = response({1, 3, 2, 0x55, 0xff});
  transaction.acknowledge(read.data());
  assert(transaction.take(output));
  equal(output, {1, 6, 3, 28, 0x55, 0xfe});
  // Preserve all possible Chrono programs, independently of the enabled flag.
  for (unsigned program = 0; program < 256; ++program) {
    for (uint8_t enabled = 0; enabled < 2; ++enabled) {
      uint8_t chrono[] = {1, 6, 5, 34, 0, enabled};
      assert(transaction.begin(chrono));
      read = response({1, 3, 2, uint8_t(program), uint8_t(1 - enabled)});
      transaction.acknowledge(read.data());
      assert(transaction.take(output));
      equal(output, {1, 6, 5, 34, uint8_t(program), enabled});
    }
  }
  // Error/timeout/disconnect all cancel the prerequisite and its desired write.
  for (unsigned failure = 0; failure < 3; ++failure) {
    uint8_t on[] = {1, 6, 3, 28, 0, 1};
    assert(transaction.begin(on));
    read = response({1, 0x83, 2});
    assert(Modbus::validate(read.data(), read.size(), on) == Modbus::Response::Exception);
    transaction.cancel();
    assert(!transaction.pending());
    assert(!transaction.take(output));
  }
  uint8_t power[] = {1, 6, 5, 41, 6, 3};
  assert(transaction.begin(power));
  equal(power, {1, 6, 5, 35, 0, 1});
  auto echo = response({1, 6, 5, 35, 0, 1});
  assert(Modbus::validate(echo.data(), echo.size(), power) == Modbus::Response::Write);
  transaction.acknowledge(echo.data());
  assert(transaction.take(output));
  equal(output, {1, 6, 5, 41, 6, 3});
  // Wrong echo/CRC cannot confirm a prerequisite.
  uint8_t another[] = {1, 6, 5, 41, 2, 5};
  assert(transaction.begin(another));
  auto wrong = response({1, 6, 5, 35, 1, 1});
  assert(Modbus::validate(wrong.data(), wrong.size(), another) == Modbus::Response::Invalid);
  assert(!transaction.take(output));
  echo.back() ^= 1;
  assert(Modbus::validate(echo.data(), echo.size(), another) == Modbus::Response::Invalid);
  transaction.cancel();
  assert(!transaction.take(output));
  // Four-byte warning reads fit the existing 32-byte encrypted envelope.
  uint8_t warnings[] = {1, 3, 2, 244, 0, 2};
  auto flags = response({1, 3, 4, 0x10, 0x05, 0x80, 0x20});
  assert(flags.size() == 9);
  assert(Modbus::validate(flags.data(), flags.size(), warnings) == Modbus::Response::Read);
  assert(pelletReserve(flags.data() + 3));
  puts("Stove protocol regression tests passed");
}
