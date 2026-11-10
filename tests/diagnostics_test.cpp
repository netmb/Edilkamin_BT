#include "../src/Diagnostics.h"
#include <assert.h>
#include <string.h>

int main() {
  using namespace Diagnostics;
  uint8_t command[6];
  request(command, MotherboardApplicationVersion);
  const uint8_t expected[] = {1, 3, 0, 0x32, 0, 8};
  assert(!memcmp(command, expected, sizeof(expected)));
  request(command, WorkingHoursTotal);
  assert(command[2] == 3 && command[3] == 0x35 && command[5] == 5);

  char out[32];
  // Device text is a little-endian char array exposed as big-endian registers.
  const uint8_t model[] = {'X','L','3','0','0','2','2',' ', '0','.',0,0,0,0,0,0};
  assert(decode(MotherboardName, model, sizeof(model), out, sizeof(out)));
  assert(!strcmp(out, "LX0320 2.0"));
  uint8_t text[16] = {0};
  assert(!decode(MotherboardName, text, sizeof(text), out, sizeof(out)));
  memset(text, 0xff, sizeof(text));
  assert(!decode(MotherboardName, text, sizeof(text), out, sizeof(out)));
  memset(text, ' ', sizeof(text));
  assert(!decode(MotherboardName, text, sizeof(text), out, sizeof(out)));
  memset(text, 0, sizeof(text));
  text[1] = '0';
  assert(decode(MotherboardName, text, sizeof(text), out, sizeof(out)));
  assert(!strcmp(out, "0"));
  text[1] = 0x1f;
  assert(!decode(MotherboardName, text, sizeof(text), out, sizeof(out)));
  assert(!decode(MotherboardName, model, sizeof(model) - 1, out, sizeof(out)));
  assert(!decode(MotherboardName, model, sizeof(model), out, 4));

  const uint8_t alarm[] = {3, 7};
  assert(decode(AlarmRaw, alarm, 2, out, sizeof(out)) && !strcmp(out, "775"));
  assert(decode(OperatingSubstate, alarm, 2, out, sizeof(out)) && !strcmp(out, "7"));
  // Alarm history count shares its register with an unrelated high-byte index.
  assert(decode(LoggedAlarmCount, alarm, 2, out, sizeof(out)) && !strcmp(out, "7"));
  assert(decode(OperationalSubphase, alarm, 2, out, sizeof(out)) && !strcmp(out, "3"));
  const uint8_t hw[] = {0x99, 20};
  assert(decode(MotherboardHardwareRevision, hw, 2, out, sizeof(out)) && !strcmp(out, "2.0"));
  const uint8_t zero[] = {0, 0};
  assert(decode(IgnitionCount, zero, 2, out, sizeof(out)) && !strcmp(out, "0"));
  uint8_t hours[10];
  memset(hours, 0xff, sizeof(hours));
  assert(decode(WorkingHoursTotal, hours, sizeof(hours), out, sizeof(out)));
  assert(!strcmp(out, "327675")); // sum must not overflow a uint16_t
  assert(!decode(WorkingHoursTotal, hours, sizeof(hours) - 2, out, sizeof(out)));
  assert(!decode(IgnitionCount, alarm, 2, out, 2) && out[0] == '\0');
  assert(!decode(Count, alarm, 2, out, sizeof(out)));
  assert(!decode(IgnitionCount, nullptr, 2, out, sizeof(out)));
}
