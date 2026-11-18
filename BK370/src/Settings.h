#pragma once
#include <stdint.h>

namespace bk370 {
struct MotorSettings {
  uint8_t dir, pwm, encoderA, encoderB;
  bool reverseMotor, reverseEncoder;
  float countsPerMeter;
  float dutyMultiplier;
};
constexpr MotorSettings Motors[2] = {
    {1, 2, 4, 5, false, false, 0, 1.0f},
    {42, 41, 6, 7, false, false, 0, 1.0f}
};
// Enable only after checking the power supply and hardware current limiting.
// Position commands additionally require calibration and a fresh center.
constexpr bool DriveCommissioned = false;
constexpr uint8_t ServoPin = 21, RedPin = 16, GreenPin = 17;
constexpr uint8_t SdaPin = 8, SclPin = 9, InaAddress = 0x40;
constexpr float ShuntOhms = 0.010f; // R010. Verify the installed shunt.
constexpr uint16_t EmptyMv = 10000, FullMv = 12400;
constexpr uint16_t CutoffMv = EmptyMv + (FullMv - EmptyMv + 9) / 10;
constexpr uint16_t RecoveryMv = CutoffMv + 260;
constexpr uint8_t Channel = 6;
constexpr char SSID[] = "Motor-Control";
constexpr char Password[] = "motor8266";
// Enter the station MAC (printed on boot) of each board. Single ignores these.
constexpr uint8_t MainMac[6] = {};
constexpr uint8_t RemoteMac[6] = {};
constexpr float LimitM = 8.0f;
constexpr uint32_t RampUpMs = 500, RampDownMs = 450;
constexpr float SlowZoneM = 0.45f;
}
