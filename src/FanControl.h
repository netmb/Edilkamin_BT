#ifndef EDILKAMIN_FAN_CONTROL_H
#define EDILKAMIN_FAN_CONTROL_H

#include <stdint.h>
#include <string.h>

// The Mind 1.3.6: 0x0548 low byte describes the fan configuration.
// Code 4 is the special configuration with only fans 2 and 3.
class FanControl {
public:
  static const uint8_t UnknownEquipment = 0xff;

  static bool supports(uint8_t equipment, uint8_t fan) {
    if (fan < 1 || fan > 3 || equipment > 4)
      return false;
    return equipment == 4 ? fan != 1 : fan <= equipment;
  }

  static const char *mode(uint8_t level) {
    static const char *const modes[] = {
        "None", "20%", "40%", "60%", "80%", "100%", "Auto"};
    return level <= 6 ? modes[level] : "None";
  }

  static uint8_t parseMode(const char *text) {
    for (uint8_t level = 1; level <= 6; ++level)
      if (strcmp(text, mode(level)) == 0)
        return level;
    return 0; // Never write zero/unknown input as a requested fan speed.
  }

  uint8_t equipment() const { return equipment_; }
  bool equipmentKnown() const { return equipment_ != UnknownEquipment; }
  bool hasFan(uint8_t fan) const { return supports(equipment_, fan); }
  bool hasExtraFans() const { return hasFan(2) || hasFan(3); }
  bool pairValid() const { return pairValid_; }
  uint8_t fan2() const { return fan2_; }
  uint8_t fan3() const { return fan3_; }
  bool relaxKnown() const { return relaxKnown_; }
  bool relax() const { return relax_; }
  bool pending() const { return pending2_ != 0 || pending3_ != 0; }

  // Cached capabilities allow commands during the normal BLE sleep interval.
  // Fresh values and settings are still mandatory before transmitting them.
  bool canAccept(uint8_t fan) const {
    return hasFan(fan) && relaxKnown_ && !relax_;
  }
  bool ready() const {
    return equipmentFresh_ && relaxFresh_ && pairValid_ && !relax_;
  }
  bool settingsFresh() const { return equipmentFresh_ && relaxFresh_; }

  void setEquipment(uint8_t code) {
    const uint8_t next = code <= 4 ? code : UnknownEquipment;
    if (next != equipment_) {
      cancelPending();
      pairValid_ = false;
    }
    equipment_ = next;
    equipmentFresh_ = equipmentKnown();
  }

  void setRelax(bool active) {
    relax_ = active;
    relaxKnown_ = relaxFresh_ = true;
    if (active)
      cancelPending();
  }

  bool updatePair(uint8_t fan3, uint8_t fan2) {
    if (fan2 > 6 || fan3 > 6) {
      failWrite();
      return false;
    }
    fan2_ = fan2;
    fan3_ = fan3;
    pairValid_ = true;
    inFlight_ = false;
    return true;
  }

  bool request(uint8_t fan, uint8_t level) {
    if ((fan != 2 && fan != 3) || level < 1 || level > 6 || !canAccept(fan))
      return false;
    (fan == 2 ? pending2_ : pending3_) = level;
    return true;
  }

  bool beginWrite(uint8_t command[6]) {
    if (!pending() || !ready() || inFlight_)
      return false;
    const uint8_t data[] = {1, 6, 5, 42,
                           pending3_ ? pending3_ : fan3_,
                           pending2_ ? pending2_ : fan2_};
    memcpy(command, data, sizeof(data));
    cancelPending();
    inFlight_ = true;
    return true;
  }

  void cancelPending() { pending2_ = pending3_ = 0; }

  // An uncertain write must never supply the partner byte for another write.
  void failWrite() {
    cancelPending();
    pairValid_ = false;
    inFlight_ = false;
  }

  void requireRefresh() {
    pairValid_ = equipmentFresh_ = relaxFresh_ = false;
    inFlight_ = false;
  }

private:
  uint8_t equipment_ = UnknownEquipment;
  uint8_t fan2_ = 0, fan3_ = 0;
  uint8_t pending2_ = 0, pending3_ = 0;
  bool equipmentFresh_ = false, pairValid_ = false;
  bool relaxKnown_ = false, relaxFresh_ = false, relax_ = false;
  bool inFlight_ = false;
};

#endif
