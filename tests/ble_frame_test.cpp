#include "BleFrame.h"
#include <assert.h>
#include <initializer_list>
#include <limits>

int main() {
  using namespace BleFrame;
  for (size_t size = 0; size < 100; ++size) {
    assert(encryptedSizeValid(size) == (size == 32 || size == 48 || size == 64));
  }
  assert(!encryptedSizeValid(std::numeric_limits<size_t>::max()));
  const uint8_t metadata[] = {1, 3, 0, 0x12, 0, 8};
  const uint8_t readOne[] = {1, 3, 2, 0xe5, 0, 1};
  const uint8_t readSixteen[] = {1, 3, 0, 0x12, 0, 16};
  const uint8_t on[] = {1, 6, 3, 0x1c, 0, 1};
  const uint8_t reset[] = {1, 0x10, 3, 0x1c, 0, 1, 2, 0xa5, 3};
  assert(expectedResponseSize(readOne) == 32);
  assert(expectedResponseSize(metadata) == 48);
  assert(expectedResponseSize(readSixteen) == 64);
  assert(expectedResponseSize(on) == 32);
  assert(expectedResponseSize(reset) == 32);
  assert(expectedResponseSize(nullptr) == 0);
  uint8_t read[] = {1, 3, 0, 0, 0, 0};
  for (unsigned count = 0; count <= 0xffff; ++count) {
    read[4] = uint8_t(count >> 8);
    read[5] = uint8_t(count);
    const size_t expected = count == 0 || count > 16 ? 0 :
                            count <= 3 ? 32 : count <= 11 ? 48 : 64;
    assert(expectedResponseSize(read) == expected);
  }
  for (size_t size = 0; size <= 80; ++size) {
    assert(notificationSizeValid(size, metadata) == (size == 32 || size == 48));
    assert(notificationSizeValid(size, readSixteen) == (size == 32 || size == 64));
    assert(notificationSizeValid(size, on) == (size == 32));
  }
  const uint8_t unknown[] = {1, 4, 0, 0, 0, 1};
  const uint8_t emptyRead[] = {1, 3, 0, 0, 0, 0};
  assert(!notificationSizeValid(32, unknown));
  assert(!notificationSizeValid(32, emptyRead));
  assert(!notificationSizeValid(32, nullptr));

  uint8_t header[20], packet[32];
  for (unsigned i = 0; i < sizeof(header); ++i) header[i] = uint8_t(i);
  // Metadata request and CRC are the literal consumer APK packet.
  const uint8_t metadataTail[] = {1, 3, 0, 0x12, 0, 8, 0xe4, 9, 4, 4, 4, 4};
  assert(buildPlaintext(packet, header, metadata, sizeof(metadata)));
  assert(memcmp(packet, header, 20) == 0);
  assert(memcmp(packet + 20, metadataTail, sizeof(metadataTail)) == 0);
  assert(validPadding(packet, sizeof(packet)));
  const uint8_t onTail[] = {1, 6, 3, 0x1c, 0, 1, 0x89, 0x88, 4, 4, 4, 4};
  assert(buildPlaintext(packet, header, on, sizeof(on)));
  assert(memcmp(packet + 20, onTail, sizeof(onTail)) == 0);
  const uint8_t resetTail[] = {1, 0x10, 3, 0x1c, 0, 1, 2, 0xa5, 3, 0xac, 0x5d, 1};
  assert(buildPlaintext(packet, header, reset, sizeof(reset)));
  assert(memcmp(packet, header, 20) == 0);
  assert(memcmp(packet + 20, resetTail, sizeof(resetTail)) == 0);
  assert(validPadding(packet, sizeof(packet)));
  const uint8_t resetEcho[] = {1, 0x10, 3, 0x1c, 0, 1};
  assert(Modbus::crc16(resetEcho, sizeof(resetEcho)) == 0x4bc0);

  // Invalid commands must leave the caller's buffer untouched.
  uint8_t unchanged[32];
  memcpy(unchanged, packet, sizeof(packet));
  for (size_t size : {size_t(0), size_t(1), size_t(5), size_t(7), size_t(8), size_t(10)})
    assert(!buildPlaintext(packet, header, reset, size));
  uint8_t badWrite[9];
  memcpy(badWrite, reset, sizeof(reset));
  badWrite[5] = 2;
  assert(!buildPlaintext(packet, header, badWrite, sizeof(badWrite)));
  badWrite[5] = 1; badWrite[6] = 4;
  assert(!buildPlaintext(packet, header, badWrite, sizeof(badWrite)));
  assert(!buildPlaintext(packet, header, unknown, sizeof(unknown)));
  assert(!buildPlaintext(packet, nullptr, reset, sizeof(reset)));
  assert(!buildPlaintext(packet, header, nullptr, sizeof(reset)));
  assert(!buildPlaintext(nullptr, header, reset, sizeof(reset)));
  assert(memcmp(packet, unchanged, sizeof(packet)) == 0);

  // Complete decrypted response shapes: short read, metadata and 16 registers.
  for (size_t size : {size_t(32), size_t(48), size_t(64)}) {
    uint8_t plain[64] = {};
    const size_t used = size == 32 ? 27 : size == 48 ? 41 : 57;
    memset(plain + used, int(size - used), size - used);
    assert(validPadding(plain, size));
    plain[used] ^= 1;
    assert(!validPadding(plain, size));
    plain[size - 1] = 0;
    assert(!validPadding(plain, size));
    plain[size - 1] = 17;
    assert(!validPadding(plain, size));
    memset(plain + size - 16, 16, 16);
    assert(validPadding(plain, size)); // Full PKCS#7 padding block.
  }
  assert(!validPadding(nullptr, 32));
  for (size_t size : {size_t(0), size_t(1), size_t(16), size_t(31), size_t(33), size_t(65)})
    assert(!validPadding(packet, size)); // Size checked before dereferencing.
}
