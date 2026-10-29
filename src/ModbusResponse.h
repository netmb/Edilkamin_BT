#ifndef EDILKAMIN_MODBUS_RESPONSE_H
#define EDILKAMIN_MODBUS_RESPONSE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Modbus {
enum class Response { Invalid, Read, Write, Exception };

inline uint16_t crc16(const uint8_t *bytes, size_t size) {
  uint16_t crc = 0xffff;
  while (size--) {
    crc ^= *bytes++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
  }
  return crc;
}

// Inspect the decrypted Modbus portion, including CRC and trailing padding.
// A read response does not carry its register address; requests stay serialized.
inline Response validate(const uint8_t *frame, size_t size,
                         const uint8_t request[6]) {
  if (size < 5 || frame[0] != request[0])
    return Response::Invalid;
  size_t dataSize;
  Response result;
  if (frame[1] == (request[1] | 0x80)) {
    dataSize = 3;
    result = Response::Exception;
  } else if (frame[1] == 3 && request[1] == 3) {
    const unsigned count = (unsigned(request[4]) << 8) | request[5];
    if (count == 0 || count > 125 || frame[2] != count * 2)
      return Response::Invalid;
    dataSize = 3 + frame[2];
    result = Response::Read;
  } else if (frame[1] == 6 && request[1] == 6) {
    if (size < 8 || memcmp(frame, request, 6) != 0)
      return Response::Invalid;
    dataSize = 6;
    result = Response::Write;
  } else {
    return Response::Invalid;
  }
  if (dataSize + 2 > size)
    return Response::Invalid;
  const uint16_t expected = crc16(frame, dataSize);
  return frame[dataSize] == (expected & 0xff) &&
                 frame[dataSize + 1] == (expected >> 8)
             ? result : Response::Invalid;
}
} // namespace Modbus

#endif
