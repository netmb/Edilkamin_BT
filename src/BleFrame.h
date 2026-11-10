#ifndef EDILKAMIN_BLE_FRAME_H
#define EDILKAMIN_BLE_FRAME_H

#include "ModbusResponse.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace BleFrame {
constexpr size_t MaxSize = 64;

inline bool encryptedSizeValid(size_t size) {
  return size >= 32 && size <= MaxSize && size % 16 == 0;
}

// The response envelope is 20 bytes. PKCS#7 always adds padding, even if
// envelope + Modbus frame already occupies an exact number of AES blocks.
inline size_t expectedResponseSize(const uint8_t request[6]) {
  if (!request) return 0;
  size_t frameSize;
  if (request[1] == 3) {
    const unsigned count = (unsigned(request[4]) << 8) | request[5];
    if (count == 0 || count > 16) return 0;
    frameSize = 5 + 2 * count;
  } else if (request[1] == 6 || request[1] == 0x10) {
    frameSize = 8;
  } else {
    return 0;
  }
  const size_t padded = ((20 + frameSize) / 16 + 1) * 16;
  return padded <= MaxSize ? padded : 0;
}

inline bool notificationSizeValid(size_t size, const uint8_t request[6]) {
  const size_t expected = expectedResponseSize(request);
  // A 32-byte packet may be an exception; the Modbus validator must decide.
  return expected != 0 && encryptedSizeValid(size) &&
         (size == expected || size == 32);
}

// Requests remain 32-byte plaintext packets for the supported commands.
// Preserve the caller's envelope: its fixed header and CBC IV are paired.
inline bool buildPlaintext(uint8_t out[32], const uint8_t header[20],
                           const uint8_t *command, size_t commandSize) {
  if (!out || !header || !command) return false;
  const bool shortCommand = commandSize == 6 &&
                            (command[1] == 3 || command[1] == 6);
  const bool singleRegisterWrite = commandSize == 9 && command[1] == 0x10 &&
                                  command[4] == 0 && command[5] == 1 &&
                                  command[6] == 2;
  if (!shortCommand && !singleRegisterWrite) return false;
  memcpy(out, header, 20);
  memcpy(out + 20, command, commandSize);
  const uint16_t crc = Modbus::crc16(command, commandSize);
  out[20 + commandSize] = uint8_t(crc);
  out[21 + commandSize] = uint8_t(crc >> 8);
  const size_t used = 22 + commandSize;
  memset(out + used, int(32 - used), 32 - used);
  return true;
}

// Optional strict check of a decrypted complete frame, before stripping pad.
inline bool validPadding(const uint8_t *plaintext, size_t size) {
  if (!plaintext || !encryptedSizeValid(size)) return false;
  const uint8_t padding = plaintext[size - 1];
  if (padding == 0 || padding > 16) return false;
  for (size_t i = size - padding; i < size; ++i) {
    if (plaintext[i] != padding) return false;
  }
  return true;
}
} // namespace BleFrame
#endif
