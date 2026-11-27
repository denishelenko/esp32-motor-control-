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
// Calibrated carriage 1. The single-carriage firmware also uses this table.
constexpr MotorSettings Motors[2] = {
    {1, 2, 5, 4, false, true, 0, 1.0f, 0, WheelCircumferenceMm, 0},  // M1A, M1B, encoder A, B
    {42, 41, 7, 6, true, false, 0, 1.0f, 0, WheelCircumferenceMm, 0}  // M2A, M2B, encoder A, B
};
// Initial values for carriage 2. Verify its wiring and encoder signs separately.
constexpr MotorSettings Carriage2Motors[2] = {
    {1, 2, 5, 4, false, true, 0, 1.0f, 0, WheelCircumferenceMm, 0},
    {42, 41, 7, 6, true, false, 0, 1.0f, 0, WheelCircumferenceMm, 0},
};
constexpr const MotorSettings (&motorSettingsFor(bool carriage2))[2] {
  return carriage2 ? Carriage2Motors : Motors;
}
constexpr double NominalCountsPerWheel = 11.0 * 9.3 * 4.0;
constexpr float NominalWheelRpm = 1452.0f;
constexpr uint8_t ServoPin = 21, StrobePin = 16, GreenPin = 17, RedPin = 18;
constexpr uint16_t ServoTravel90Ms = 350;
constexpr uint8_t SdaPin = 8, SclPin = 9, InaAddress = 0x40;
constexpr float ShuntOhms = 0.010f; // R010. Verify the installed shunt.
// True when the motor supply is wired directly to the 3S battery through the shunt.
constexpr bool InaMeasuresBatteryFeed = true;  // Set false if a regulator holds the motor rail at 12 V.
constexpr uint16_t EmptyMv = 10000, FullMv = 12400;
constexpr uint16_t CutoffMv = EmptyMv + (FullMv - EmptyMv + 9) / 10;
constexpr uint16_t RecoveryMv = CutoffMv + 260;
constexpr uint8_t Channel = 6;
constexpr char HOME_WIFI_SSID[] = "";
constexpr char HOME_WIFI_PASSWORD[] = "";
constexpr char SSID[] = "Motor-Control";
constexpr char Password[] = "motor8266";
// Enter the station MAC (printed on boot) of each board. Single ignores these.
constexpr uint8_t MainMac[6] = {};
constexpr uint8_t RemoteMac[6] = {};
constexpr float LimitM = 8.0f;
constexpr float SharedCableM = 20.0f;
constexpr float SharedEdgeReserveM = 0.30f;
constexpr float SharedCenterGapM = 1.0f;
// Distance along the cable between the centres of the left and right wheels.
constexpr float WheelbaseMm = 0.0f;  // Set after measuring the actual carriage.
constexpr bool Motor1OnRight = true;
constexpr float HalfWheelbaseM = WheelbaseMm / 2000.0f;
constexpr float RequestedSharedTravelM = 9.0f;
constexpr float SharedInnerM = SharedCenterGapM / 2 + HalfWheelbaseM;
constexpr float MaxSharedOuterM = SharedCableM / 2 - SharedEdgeReserveM - HalfWheelbaseM;
constexpr float SharedOuterM = SharedInnerM + RequestedSharedTravelM < MaxSharedOuterM ?
    SharedInnerM + RequestedSharedTravelM : MaxSharedOuterM;
static_assert(WheelbaseMm >= 0 && SharedOuterM > SharedInnerM, "Wheelbase leaves no shared cable travel");
constexpr float SharedOverrunM = 0.08f;
constexpr float SharedSurveyMps = 0.30f;
constexpr float SharedBoundaryDecelMps2 = 0.8f;
constexpr uint32_t ParkIdleDelayMs = 5 * 60 * 1000;
constexpr float ParkToleranceM = 0.005f;
constexpr uint8_t ParkCorrectionLimit = 3;
constexpr float DefaultSpeedKmh = 5.0f;
constexpr bool speedPreset(int kmh) { return kmh == 5 || kmh == 10 || kmh == 15; }
constexpr float DutyRisePerSec = 0.50f;
// Initial estimate of the duty needed to overcome drivetrain friction.
constexpr uint8_t CalibratedStartPwmPercent[2] = {8, 8};
constexpr uint8_t CalibratedApproachPwmPercent[2] = {15, 15};
constexpr uint8_t DefaultMaxPwmPercent = 20;
constexpr uint8_t MinDrivePercent = 5;
constexpr float drivePwmFraction(uint8_t percent) {
  return percent == 0 ? 0.0f : (MinDrivePercent + (100.0f - MinDrivePercent) * (percent - 1) / 99.0f) / 100.0f;
}
constexpr uint32_t StartBoostMs = 250;
constexpr float ApproachTurnsPerSecond = 1.0f;
constexpr float ApproachTurns = 0.25f;
constexpr float AccelTurnsPerSecondSquared = 8.0f;
constexpr float DecelTurnsPerSecondSquared = 5.0f;
constexpr float SpeedKp = 0.045f;
constexpr float SpeedKi = 0.12f;
constexpr uint32_t RampUpMs = 500, RampDownMs = 450;
constexpr float SlowZoneM = 1.0f;
constexpr float FinalBrakeM = 0.03f;
constexpr float TargetToleranceM = 0.01f;
constexpr float ProgramOvershootToleranceM = 0.05f;
constexpr float ApproachMinScale = 0.12f;
constexpr float ApproachBrakeMarginMps = 0.025f;
constexpr float FinalCreepMps = 0.10f;
constexpr uint32_t BrakeQuietMs = 200, FinalApproachTimeoutMs = 5000;
static_assert(SlowZoneM > FinalBrakeM && FinalBrakeM > TargetToleranceM && TargetToleranceM > 0);
static_assert(ApproachMinScale > 0 && ApproachMinScale < 1);
}
