#ifndef EDILKAMIN_STOVE_PROTOCOL_H
#define EDILKAMIN_STOVE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace StoveProtocol {
inline uint16_t word(const uint8_t *data) {
  return (uint16_t(data[0]) << 8) | data[1];
}
inline void temperature(uint16_t tenths, char *out, size_t size) {
  snprintf(out, size, "%u.%u", unsigned(tenths / 10), unsigned(tenths % 10));
}
// Decimal Celsius only. Extra fractional digits are allowed only if zero:
// 24, 24.0 and 24.50 are exact; 24.55 is not representable in this register.
inline bool parseTemperature(const char *text, uint16_t &tenths) {
  if (!text || *text < '0' || *text > '9') return false;
  unsigned whole = 0;
  while (*text >= '0' && *text <= '9') {
    whole = whole * 10 + (*text++ - '0');
    if (whole > 35) return false;
  }
  unsigned fraction = 0;
  if (*text == '.') {
    ++text;
    if (*text < '0' || *text > '9') return false;
    fraction = *text++ - '0';
    while (*text == '0') ++text;
  }
  const unsigned value = whole * 10 + fraction;
  if (*text || value < 50 || value > 350) return false;
  tenths = value;
  return true;
}
inline bool pelletReserve(const uint8_t *data) { return (data[1] & 4) != 0; }
inline void statusText(uint8_t sub, uint8_t main, char *out, size_t size) {
  const char *text = nullptr;
  if (main == 0 && sub == 0) text = "Off";
  else if (main == 1) {
    switch (sub) {
    case 0: case 1: case 2: text = "Cleaning"; break;
    case 3: text = "Loading Pellets"; break;
    case 4: text = "Ignition - charge suspend"; break;
    case 5: text = "Check exhaust gas temperature"; break;
    case 6: text = "Threshold temperature check"; break;
    case 7: text = "Warmup"; break;
    }
  } else if (main == 2 && sub == 2) text = "Ok";
  else if (main == 3 && sub == 0) text = "Shutdown";
  else if (main == 4 && sub == 0) text = "Extinction";
  else if (main == 5) text = "Final cleaning";
  if (text) snprintf(out, size, "%s", text);
  else snprintf(out, size, "Unknown (main=%u, sub=%u)", main, sub);
}

// A serialized read-before-write transaction. The caller validates the Modbus
// response against the prerequisite request before calling acknowledge().
// Never reuse a cached shared register across user commands or reconnects.
class ControlTransaction {
public:
  bool pending() const { return state_ != Idle; }
  void cancel() { state_ = Idle; }
  bool begin(uint8_t command[6]) {
    if (pending() || command[1] != 6) return false;
    const uint16_t reg = word(command + 2);
    if (reg != 0x031c && reg != 0x0522 && reg != 0x0529) return false;
    memcpy(desired_, command, 6);
    state_ = Waiting;
    if (reg == 0x0529) {
      // Manual power: confirm Automatic OFF before changing the power level.
      const uint8_t automaticOff[] = {1, 6, 5, 35, 0, 1};
      memcpy(command, automaticOff, 6);
    } else {
      command[1] = 3;
      command[4] = 0;
      command[5] = 1;
    }
    return true;
  }
  void acknowledge(const uint8_t *frame) {
    if (state_ != Waiting) return;
    const uint16_t reg = word(desired_ + 2);
    if (reg == 0x0529) {
      const uint8_t expected[] = {1, 6, 5, 35, 0, 1};
      if (memcmp(frame, expected, 6)) { cancel(); return; }
    } else {
      if (frame[1] != 3 || frame[2] != 2) { cancel(); return; }
      desired_[4] = frame[3];
      if (reg == 0x031c)
        desired_[5] = (frame[4] & 0xfe) | (desired_[5] & 1);
    }
    state_ = Ready;
  }
  bool take(uint8_t command[6]) {
    if (state_ != Ready) return false;
    memcpy(command, desired_, 6);
    cancel();
    return true;
  }
private:
  enum State { Idle, Waiting, Ready } state_ = Idle;
  uint8_t desired_[6] = {};
};
} // namespace StoveProtocol
#endif
