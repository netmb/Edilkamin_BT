#include "AlarmReset.h"
#include "ModbusResponse.h"
#include <assert.h>
#include <initializer_list>
#include <vector>

using AlarmReset::Result;
using AlarmReset::Transaction;

static void accept(Transaction &t, std::initializer_list<uint8_t> bytes) {
  std::vector<uint8_t> frame(bytes);
  const uint16_t crc = Modbus::crc16(frame.data(), frame.size());
  frame.push_back(uint8_t(crc));
  frame.push_back(uint8_t(crc >> 8));
  t.accept(frame.data(), frame.size());
}

static void request(Transaction &t, std::initializer_list<uint8_t> expected) {
  uint8_t out[9] = {};
  size_t len = 99;
  assert(t.busy() && t.requestReady());
  assert(t.take(out, len));
  assert(len == expected.size());
  assert(memcmp(out, expected.begin(), len) == 0);
  assert(!t.requestReady());
  assert(!t.take(out, len) && len == 0); // Never retransmit a write.
  assert(!t.begin()); // A second button press cannot replace a transaction.
}

static void start(Transaction &t) {
  assert(t.begin());
  request(t, {1, 3, 2, 0xe5, 0, 1});
  accept(t, {1, 3, 2, 7, 0});
  request(t, {1, 3, 3, 0x1c, 0, 1});
}

int main() {
  Transaction t;
  assert(!t.busy() && !t.requestReady());
  // The state is the high byte; a low-byte 7 is not an active alarm state.
  assert(t.begin());
  request(t, {1, 3, 2, 0xe5, 0, 1});
  accept(t, {1, 3, 2, 0, 7});
  assert(t.result() == Result::NotAlarm && !t.requestReady());

  // Preserve all other flags for eligible OFF commands. An existing ON command
  // must never be acknowledged or silently changed to OFF by this transaction.
  for (unsigned flags = 0; flags <= 0xffff; ++flags) {
    start(t);
    accept(t, {1, 3, 2, uint8_t(flags >> 8), uint8_t(flags)});
    if (flags & 2) {
      assert(t.result() == Result::AlreadySet && !t.requestReady());
    } else if (flags & 1) {
      assert(t.result() == Result::PowerOn && !t.requestReady() && !t.busy());
      uint8_t out[9]; size_t len = 99;
      assert(!t.take(out, len) && len == 0);
    } else {
      request(t, {1, 0x10, 3, 0x1c, 0, 1, 2,
                  uint8_t(flags >> 8), uint8_t(flags | 2)});
      t.fail();
      assert(!t.busy() && t.result() == Result::Failed);
    }
  }

  // A write acknowledgement alone is never reported as cleared. Both fresh
  // reads are mandatory, and either persisting signal means StillActive.
  for (uint8_t state : {uint8_t(0), uint8_t(7), uint8_t(3)}) {
    for (uint8_t alarm : {uint8_t(0), uint8_t(21)}) {
      start(t);
      accept(t, {1, 3, 2, 0xa5, 0});
      request(t, {1, 0x10, 3, 0x1c, 0, 1, 2, 0xa5, 2});
      accept(t, {1, 0x10, 3, 0x1c, 0, 1});
      assert(t.result() == Result::Pending);
      request(t, {1, 3, 2, 0xe5, 0, 1});
      accept(t, {1, 3, 2, state, 7});
      assert(t.result() == Result::Pending);
      request(t, {1, 3, 2, 0xe6, 0, 1});
      accept(t, {1, 3, 2, alarm, 21}); // Alarm code is also the high byte.
      assert(t.result() == (state != 7 && alarm == 0 ? Result::Cleared
                                                    : Result::StillActive));
      assert(!t.requestReady()); // No pulse clear or automatic ignition.
    }
  }

  // Failure at every request boundary leaves no queued write or retry.
  for (unsigned step = 0; step < 5; ++step) {
    assert(t.begin());
    for (unsigned current = 0; current <= step; ++current) {
      uint8_t out[9]; size_t len;
      assert(t.take(out, len));
      if (current == step) t.fail();
      else if (current == 0) accept(t, {1, 3, 2, 7, 0});
      else if (current == 1) accept(t, {1, 3, 2, 0, 0});
      else if (current == 2) accept(t, {1, 0x10, 3, 0x1c, 0, 1});
      else accept(t, {1, 3, 2, 0, 0});
    }
    assert(!t.busy() && !t.requestReady() && t.result() == Result::Failed);
    accept(t, {1, 3, 2, 7, 0}); // Late response cannot resurrect a failure.
    assert(!t.busy());
  }

  // Defense in depth for caller mistakes; CRC validation belongs to caller.
  assert(t.begin());
  request(t, {1, 3, 2, 0xe5, 0, 1});
  t.accept(nullptr, 7);
  assert(t.result() == Result::Failed);
  for (auto bad : {std::vector<uint8_t>{1, 3, 2, 7},
                   std::vector<uint8_t>{1, 0x83, 2, 0, 0},
                   std::vector<uint8_t>{2, 3, 2, 7, 0, 0, 0},
                   std::vector<uint8_t>{1, 3, 4, 7, 0, 0, 0}}) {
    assert(t.begin());
    request(t, {1, 3, 2, 0xe5, 0, 1});
    t.accept(bad.data(), bad.size());
    assert(t.result() == Result::Failed);
  }
  start(t);
  accept(t, {1, 3, 2, 0, 0});
  request(t, {1, 0x10, 3, 0x1c, 0, 1, 2, 0, 2});
  accept(t, {1, 0x10, 3, 0x1d, 0, 1}); // Wrong echoed register.
  assert(t.result() == Result::Failed);
}
