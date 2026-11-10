#ifndef EDILKAMIN_ALARM_RESET_H
#define EDILKAMIN_ALARM_RESET_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace AlarmReset {
enum class Result { Pending, NotAlarm, AlreadySet, PowerOn, Cleared, StillActive, Failed };

// Manual acknowledgement only. The caller serializes these requests with all
// other operations and validates each response's Modbus CRC before accept().
// A timeout, disconnect, exception or write failure must call fail().
class Transaction {
public:
  bool busy() const { return result_ == Result::Pending; }
  bool requestReady() const { return busy() && !awaiting_; }
  Result result() const { return result_; }

  bool begin() {
    if (busy()) return false;
    result_ = Result::Pending;
    step_ = CheckState;
    awaiting_ = false;
    verifiedState_ = 7;
    return true;
  }

  // Payload excludes CRC and the encrypted envelope. The output needs 9 bytes.
  bool take(uint8_t out[9], size_t &len) {
    len = 0;
    if (!requestReady() || !out) return false;
    if (step_ == WriteReset) {
      const uint8_t write[] = {1, 0x10, 3, 0x1c, 0, 1, 2,
                               commandsHigh_, uint8_t(commandsLow_ | 2)};
      memcpy(out, write, sizeof(write));
      len = sizeof(write);
    } else {
      const uint16_t reg = step_ == ReadCommands ? 0x031c :
                           step_ == VerifyAlarm ? 0x02e6 : 0x02e5;
      const uint8_t read[] = {1, 3, uint8_t(reg >> 8), uint8_t(reg), 0, 1};
      memcpy(out, read, sizeof(read));
      len = sizeof(read);
    }
    awaiting_ = true;
    return true;
  }

  void accept(const uint8_t *frame, size_t size) {
    if (!busy() || !awaiting_) return;
    if (step_ == WriteReset) {
      const uint8_t echo[] = {1, 0x10, 3, 0x1c, 0, 1};
      if (!frame || size < 8 || memcmp(frame, echo, sizeof(echo))) {
        fail();
        return;
      }
      step_ = VerifyState;
    } else {
      if (!frame || size < 7 || frame[0] != 1 || frame[1] != 3 ||
          frame[2] != 2) {
        fail();
        return;
      }
      switch (step_) {
      case CheckState:
        if (frame[3] != 7) finish(Result::NotAlarm);
        else step_ = ReadCommands;
        break;
      case ReadCommands:
        commandsHigh_ = frame[3];
        commandsLow_ = frame[4];
        if (commandsLow_ & 2) finish(Result::AlreadySet);
        // A retained ON command could cause ignition after acknowledgement.
        // Require a separate, explicit OFF action rather than changing bit 0.
        else if (commandsLow_ & 1) finish(Result::PowerOn);
        else step_ = WriteReset;
        break;
      case VerifyState:
        verifiedState_ = frame[3];
        step_ = VerifyAlarm;
        break;
      case VerifyAlarm:
        finish(verifiedState_ != 7 && frame[3] == 0 ? Result::Cleared
                                                  : Result::StillActive);
        break;
      case WriteReset:
        break;
      }
    }
    awaiting_ = false;
  }

  void fail() { finish(Result::Failed); }

private:
  enum Step { CheckState, ReadCommands, WriteReset, VerifyState, VerifyAlarm };
  void finish(Result result) {
    result_ = result;
    awaiting_ = false;
  }
  Result result_ = Result::Failed;
  Step step_ = CheckState;
  bool awaiting_ = false;
  uint8_t commandsHigh_ = 0;
  uint8_t commandsLow_ = 0;
  uint8_t verifiedState_ = 7;
};
} // namespace AlarmReset
#endif
