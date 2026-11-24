#pragma once
#include <stdint.h>

namespace bk370 {
struct MotorSettings {
  uint8_t pwmA, pwmB, encoderA, encoderB;
  bool reverseMotor, reverseEncoder;
  float countsPerMeter;
  float dutyMultiplier;
  float maxCountsPerSecond;
  float wheelCircumferenceMm;
  double countsPerWheelTurn;

  constexpr float effectiveCountsPerMeter() const {
    if (countsPerMeter > 0) return countsPerMeter;
    if (wheelCircumferenceMm <= 0 || countsPerWheelTurn <= 0) return 0;
    return countsPerWheelTurn * 1000.0f / wheelCircumferenceMm;
  }
};
constexpr float WheelDiameterMm = 70.0f;
constexpr float WheelCircumferenceMm = WheelDiameterMm * 3.14159265358979323846f;
constexpr MotorSettings Motors[2] = {
    {1, 2, 5, 4, false, true, 0, 1.0f, 0, WheelCircumferenceMm, 0},  // M1A, M1B, encoder A, B
    {42, 41, 7, 6, true, false, 0, 1.0f, 0, WheelCircumferenceMm, 0}  // M2A, M2B, encoder A, B
};
constexpr float CountsPerMeter[2] = {Motors[0].effectiveCountsPerMeter(), Motors[1].effectiveCountsPerMeter()};
constexpr float MetersPerCount[2] = {
    CountsPerMeter[0] > 0 ? 1.0f / CountsPerMeter[0] : 0,
    CountsPerMeter[1] > 0 ? 1.0f / CountsPerMeter[1] : 0,
};
constexpr float WheelMaxMps[2] = {
    Motors[0].maxCountsPerSecond * MetersPerCount[0],
    Motors[1].maxCountsPerSecond * MetersPerCount[1],
};
constexpr float ConfiguredMaxSpeedMps = WheelMaxMps[0] < WheelMaxMps[1] ? WheelMaxMps[0] : WheelMaxMps[1];
// Enable only after checking the power supply and hardware current limiting.
// Position commands additionally require calibration and a fresh center.
constexpr bool DriveCommissioned = false;
constexpr uint8_t ServoPin = 21, StrobePin = 16, GreenPin = 17, RedPin = 18;
constexpr uint16_t ServoTravel90Ms = 350;
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
constexpr float SlowZoneM = 1.0f;
constexpr float FinalBrakeM = 0.10f;
constexpr float TargetToleranceM = 0.01f;
constexpr float ApproachMinScale = 0.12f;
constexpr float ApproachBrakeMarginMps = 0.025f;
constexpr float FinalCreepMps = 0.10f, FinalCreepDutyLimit = 0.30f;
constexpr uint32_t BrakeQuietMs = 200, FinalApproachTimeoutMs = 5000;
static_assert(SlowZoneM > FinalBrakeM && FinalBrakeM > TargetToleranceM && TargetToleranceM > 0);
static_assert(ApproachMinScale > 0 && ApproachMinScale < 1);
}
