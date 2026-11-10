#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Read-only map from The Mind 1.3.6 SoftwareSettingsViewModel and official
// Edilkamin Service 2.4.260529 DWARF (shared_data.h/eepromData.h).
// Shared-memory byte offsets map to Modbus register = byte offset / 2.
// See .local/research/service-diagnostics-map.txt for evidence and limits.
namespace Diagnostics {
enum Kind { Text, U8High, U8Low, U16, U8LowTenths, SumU16 };
enum Id {
  MotherboardSerial, MotherboardName, MotherboardBootloader,
  MotherboardBootloaderVersion, MotherboardApplication,
  MotherboardApplicationVersion, ConnectionModuleName,
  ConnectionModuleVersion, LocalInterfaceName, LocalInterfaceVersion,
  RadioModuleName, RadioModuleVersion, CommandPanelName,
  CommandPanelVersion, MotherboardHardwareRevision, DatabaseCode,
  OperationalPhase, OperationalSubphase, OperationalDetail,
  OperatingState, OperatingSubstate, AlarmRaw, LoggedAlarmCount,
  IgnitionCount, WorkingHoursP1, WorkingHoursP2, WorkingHoursP3,
  WorkingHoursP4, WorkingHoursP5, WorkingHoursTotal, Count
};
struct Field {
  const char *key;
  const char *name;
  uint16_t reg;
  uint8_t count;
  Kind kind;
  const char *unit;
};

// componentsInfo starts at byte 0x14; each board record is 0x64 bytes.
// char[16] fields: serial +0, name +0x10, boot name/version +0x20/+0x30,
// application name/version +0x40/+0x50. Device role names follow the DWARF.
static const Field fields[Count] = {
  {"motherboard_serial", "Motherboard serial", 0x000a, 8, Text, nullptr},
  {"motherboard_name", "Motherboard name", 0x0012, 8, Text, nullptr},
  {"motherboard_bootloader", "Motherboard bootloader", 0x001a, 8, Text, nullptr},
  {"motherboard_bootloader_version", "Motherboard bootloader version", 0x0022, 8, Text, nullptr},
  {"motherboard_application", "Motherboard application", 0x002a, 8, Text, nullptr},
  {"motherboard_application_version", "Motherboard application version", 0x0032, 8, Text, nullptr},
  {"connection_module_name", "Connection module name", 0x0044, 8, Text, nullptr},
  {"connection_module_version", "Connection module version", 0x0064, 8, Text, nullptr},
  {"local_interface_name", "Local interface name", 0x0076, 8, Text, nullptr},
  {"local_interface_version", "Local interface version", 0x0096, 8, Text, nullptr},
  {"radio_module_name", "Radio module name", 0x0206, 8, Text, nullptr},
  {"radio_module_version", "Radio module version", 0x0226, 8, Text, nullptr},
  {"command_panel_name", "Command panel name", 0x0238, 8, Text, nullptr},
  {"command_panel_version", "Command panel version", 0x0258, 8, Text, nullptr},
  // status.machine starts at byte 0x5c8. Revision is uint8_t x10 at +0x34.
  {"motherboard_hardware_revision", "Motherboard hardware revision", 0x02fe, 1, U8LowTenths, nullptr},
  // nvm starts at 0x668; codiceBancaDati uint16_t at +0x5d6.
  {"database_code", "Database code", 0x061f, 1, U16, nullptr},
  // uint8_t fields at machine offsets +0,+1,+2,+3,+4,+5 respectively.
  {"operational_phase", "Operational phase", 0x02e4, 1, U8Low, nullptr},
  {"operational_subphase", "Operational subphase", 0x02e4, 1, U8High, nullptr},
  {"operational_detail", "Operational detail", 0x02e5, 1, U8Low, nullptr},
  {"operating_state", "Operating state", 0x02e5, 1, U8High, nullptr},
  {"operating_substate", "Operating substate", 0x02e6, 1, U8Low, nullptr},
  {"alarm_raw", "Alarm raw", 0x02e6, 1, U16, nullptr},
  // nvm.logAllarmi at +0xc: count uint8_t +0, ring index uint8_t +1.
  {"logged_alarm_count", "Logged alarm count", 0x033a, 1, U8Low, nullptr},
  // nvm.contatoriTot: starts uint16_t +0; work hours uint16_t[5] +2.
  {"ignition_count", "Ignition count", 0x0334, 1, U16, nullptr},
  {"working_hours_p1", "Working hours P1", 0x0335, 1, U16, "h"},
  {"working_hours_p2", "Working hours P2", 0x0336, 1, U16, "h"},
  {"working_hours_p3", "Working hours P3", 0x0337, 1, U16, "h"},
  {"working_hours_p4", "Working hours P4", 0x0338, 1, U16, "h"},
  {"working_hours_p5", "Working hours P5", 0x0339, 1, U16, "h"},
  {"working_hours_total", "Working hours total", 0x0335, 5, SumU16, "h"}
};

inline void request(uint8_t out[6], unsigned index) {
  memset(out, 0, 6);
  if (index >= Count) return;
  const Field &f = fields[index];
  out[0] = 1;
  out[1] = 3;
  out[2] = static_cast<uint8_t>(f.reg >> 8);
  out[3] = static_cast<uint8_t>(f.reg);
  out[4] = 0;
  out[5] = f.count;
}

// data is only the validated FC03 data payload (wire order, without header/CRC).
// Text reverses bytes within each register to restore the device char array.
inline bool decode(unsigned index, const uint8_t *data, size_t size,
                   char *out, size_t outsize) {
  if (!out || !outsize) return false;
  out[0] = '\0';
  if (index >= Count || !data) return false;
  const Field &f = fields[index];
  if (size != static_cast<size_t>(f.count) * 2) return false;
  if (f.kind == Text) {
    char text[17];
    if (size > sizeof(text) - 1) return false;
    size_t length = 0;
    for (size_t i = 0; i < size; ++i) {
      const uint8_t c = data[i ^ 1u];
      if (!c) break;
      if (c < 0x20 || c > 0x7e) return false;
      text[length++] = static_cast<char>(c);
    }
    while (length && text[length - 1] == ' ') --length;
    if (!length || length >= outsize) return false;
    text[length] = '\0';
    memcpy(out, text, length + 1);
    return true;
  }
  uint32_t value = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  if (f.kind == U8High) value = data[0];
  else if (f.kind == U8Low || f.kind == U8LowTenths) value = data[1];
  else if (f.kind == SumU16) {
    value = 0;
    for (size_t i = 0; i < size; i += 2)
      value += (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
  }
  int written;
  if (f.kind == U8LowTenths)
    written = snprintf(out, outsize, "%lu.%lu",
                       static_cast<unsigned long>(value / 10),
                       static_cast<unsigned long>(value % 10));
  else
    written = snprintf(out, outsize, "%lu", static_cast<unsigned long>(value));
  if (written < 0 || static_cast<size_t>(written) >= outsize) {
    out[0] = '\0';
    return false;
  }
  return true;
}
} // namespace Diagnostics
