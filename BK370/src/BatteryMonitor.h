#pragma once
#include <Wire.h>
#include "Hardware.h"

namespace bk370 {
enum InaPeakFlags : uint8_t { InaInterval = 1, InaClipped = 2, InaStartup = 4, InaStartupClipped = 8, InaBatteryFeed = 16 };
inline float batteryChargePercent(uint16_t mv) {
  return constrain(100.0f * (int(mv) - EmptyMv) / (FullMv - EmptyMv), 0.0f, 100.0f);
}
struct InaPeaks {
  float interval = 0, startup = 0;
  uint16_t sequence = 0;
  uint8_t flags = 0;
};

class BatteryMonitor {
 public:
  uint16_t millivolts = 0;
  bool valid = false, locked = false, currentValid = false;
  float amps = 0;
  InaPeaks peaks;
  void begin() {
    if (!Wire.begin(SdaPin, SclPin, 100000)) return;
    Wire.setTimeOut(4);
    xTaskCreate(runSampling, "ina226", 3072, this, 2, nullptr);
  }

  void motorPwm(uint32_t now, bool active) {
    if (!active) return;
    if (!seenPwm_ || now - lastPwmAt_ >= 250) {
      seenPwm_ = true;
      portENTER_CRITICAL(&sampleLock_);
      startAt_ = now;
      capturingStart_ = true;
      startValid_ = startClipped_ = false;
      portEXIT_CRITICAL(&sampleLock_);
    }
    lastPwmAt_ = now;
  }

  bool allowed(uint32_t now) const { return valid && !locked && now - freshAt_ <= 750; }
  void tick(uint32_t now) {
    if (now - readAt_ < 100) return;
    readAt_ = now;
    portENTER_CRITICAL(&sampleLock_);
    const auto batch = batch_;
    batch_ = {};
    peaks.startup = startPeak_;
    const bool startValid = startValid_;
    const bool startClipped = startClipped_;
    portEXIT_CRITICAL(&sampleLock_);

    valid = batch.busValid;
    currentValid = batch.count > 0;
    const bool hasPeak = currentValid || batch.clipped;
    peaks.interval = batch.peak;
    peaks.flags = (hasPeak ? InaInterval : 0) | (batch.clipped ? InaClipped : 0) |
        (startValid ? InaStartup : 0) | (startClipped ? InaStartupClipped : 0) |
        (InaMeasuresBatteryFeed ? InaBatteryFeed : 0);
    ++peaks.sequence;
    if (currentValid) amps = batch.sum / batch.count;
    if (valid) {
      millivolts = uint32_t(batch.busRaw) * 5 / 4;
      freshAt_ = now;
      if (InaMeasuresBatteryFeed && millivolts <= CutoffMv) {
        if (++lowSamples_ >= 3) { locked = true; lowSamples_ = 3; }
      } else {
        lowSamples_ = 0;
      }
    } else {
      lowSamples_ = 0;
    }
  }
  bool reset(uint32_t now) {
    if (!InaMeasuresBatteryFeed || !valid || now - freshAt_ > 750 || millivolts < RecoveryMv) return false;
    locked = false;
    lowSamples_ = 0;
    return true;
  }
  bool ready(uint32_t now) const { return allowed(now) && lowSamples_ == 0; }
 private:
  struct Batch {
    float sum = 0, peak = 0;
    uint32_t count = 0;
    uint16_t busRaw = 0;
    bool busValid = false, clipped = false;
  };

  static void runSampling(void *context) {
    static_cast<BatteryMonitor *>(context)->sampleLoop();
  }

  void sampleLoop() {
    // INA226 CONFIG: one sample, 588 us bus + 588 us shunt, continuous.
    constexpr uint16_t config = 0x40DF;
    while (!writeRegister(0, config)) vTaskDelay(pdMS_TO_TICKS(500));
    constexpr TickType_t period = pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
      if (xTaskDelayUntil(&wake, period) == pdFALSE) wake = xTaskGetTickCount();
      uint16_t shunt = 0, bus = 0;
      const bool busValid = readRegister(2, bus) && bus >= 4000 && bus <= 12000;
      const bool shuntValid = busValid && ShuntOhms > 0 && readRegister(1, shunt);
      const bool clipped = shuntValid && (shunt == 0x7fff || shunt == 0x8000);
      const float current = shuntValid ? int16_t(shunt) * 0.0000025f / ShuntOhms : 0;
      const uint32_t now = millis();
      portENTER_CRITICAL(&sampleLock_);
      if (busValid) {
        batch_.busRaw = bus;
        batch_.busValid = true;
      }
      if (shuntValid) {
        if ((!batch_.count && !batch_.clipped) || current > batch_.peak) batch_.peak = current;
        batch_.clipped |= clipped;
        if (!clipped) {
          batch_.sum += current;
          ++batch_.count;
        }
        if (capturingStart_ && now - startAt_ < 500) {
          if (!startValid_ || current > startPeak_) startPeak_ = current;
          startValid_ = true;
          startClipped_ |= clipped;
        }
      }
      if (capturingStart_ && now - startAt_ >= 500) capturingStart_ = false;
      portEXIT_CRITICAL(&sampleLock_);
    }
  }

  bool writeRegister(uint8_t address, uint16_t value) {
    Wire.beginTransmission(InaAddress);
    Wire.write(address);
    Wire.write(uint8_t(value >> 8));
    Wire.write(uint8_t(value));
    return Wire.endTransmission() == 0;
  }

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
  portMUX_TYPE sampleLock_ = portMUX_INITIALIZER_UNLOCKED;
  Batch batch_;
  float startPeak_ = 0;
  uint32_t startAt_ = 0, lastPwmAt_ = 0;
  bool seenPwm_ = false, capturingStart_ = false, startValid_ = false, startClipped_ = false;
  uint32_t readAt_ = 0, freshAt_ = 0;
  uint8_t lowSamples_ = 0;
};
}
