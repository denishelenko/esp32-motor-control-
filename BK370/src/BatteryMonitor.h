#pragma once
#include <Wire.h>
#include "Hardware.h"

namespace bk370 {
class BatteryMonitor {
 public:
  uint16_t millivolts = 0;
  bool valid = false, locked = false, currentValid = false;
  float amps = 0;
  float percent() const {
    return constrain(100.0f * (int(millivolts) - EmptyMv) / (FullMv - EmptyMv), 0.0f, 100.0f);
  }
  void begin() {
    Wire.begin(SdaPin, SclPin, 100000);
    Wire.setTimeOut(8);
  }
  bool allowed(uint32_t now) const { return valid && !locked && now - freshAt_ <= 750; }
  void tick(uint32_t now) {
    if (now - readAt_ < 100) return;
    readAt_ = now;
    uint16_t raw = 0;
    const bool wasValid = valid;
    const bool wasLocked = locked;
    valid = readRegister(2, raw) && raw >= 4000 && raw <= 12000;
    uint16_t shunt = 0;
    currentValid = valid && ShuntOhms > 0 && readRegister(1, shunt);
    if (currentValid) amps = int16_t(shunt) * 0.0000025f / ShuntOhms;
    if (valid) {
      millivolts = uint32_t(raw) * 5 / 4;
      freshAt_ = now;
      if (millivolts <= CutoffMv) {
        if (++lowSamples_ >= 3) { locked = true; lowSamples_ = 3; }
      } else {
        lowSamples_ = 0;
      }
    } else {
      lowSamples_ = 0;
    }
    if (valid != wasValid || locked != wasLocked || (valid && now - loggedAt_ >= 5000)) {
      log("BATTERY", "valid=%d mV=%u charge=%.1f%% locked=%d", valid, millivolts, percent(), locked);
      loggedAt_ = now;
    }
  }
  bool reset(uint32_t now) {
    if (!valid || now - freshAt_ > 750 || millivolts < RecoveryMv) return false;
    locked = false;
    lowSamples_ = 0;
    return true;
  }
  bool ready(uint32_t now) const { return allowed(now) && lowSamples_ == 0; }
 private:
  bool readRegister(uint8_t address, uint16_t &value) {
    Wire.beginTransmission(InaAddress);
    Wire.write(address);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(InaAddress, uint8_t(2)) != 2) return false;
    const int high = Wire.read();
    const int low = Wire.read();
    if (high < 0 || low < 0) return false;
    value = (uint16_t(high) << 8) | uint8_t(low);
    return true;
  }
  uint32_t readAt_ = 0, freshAt_ = 0, loggedAt_ = 0;
  uint8_t lowSamples_ = 0;
};
}
