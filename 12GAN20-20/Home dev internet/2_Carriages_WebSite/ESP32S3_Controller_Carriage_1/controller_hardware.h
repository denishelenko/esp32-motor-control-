#pragma once
#include "event_debug.h"

// ESP32-S3 motor, servo and indicator outputs.

static bool servoStrobeActive = false;
static uint32_t servoStrobeStartedAt = 0;

static void setServoStrobe(uint8_t commandAngle) {
  const bool active = commandAngle > 0 && commandAngle < 180;
  if (active == servoStrobeActive) return;
  servoStrobeActive = active;
  servoStrobeStartedAt = millis();
  digitalWrite(MOSFET_PIN, active ? HIGH : LOW);
}

static void updateServoStrobe(uint32_t now) {
  if (servoStrobeActive) {
    const bool on = ((now - servoStrobeStartedAt) / LIGHT_BLINK_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(MOSFET_PIN, on ? HIGH : LOW);
  }
}

void writeLocalMotor(int16_t pwm) {
  pwm = constrain(pwm, -int16_t(PWM_MAX), int16_t(PWM_MAX));
  diagnosticMotor(pwm);
  if (MOTOR_DIRECTION_REVERSED) pwm = -pwm;
  // Only disable PWM when the electrical direction changes.
  static int8_t previousDirection = 0;
  const int8_t direction = (pwm > 0) - (pwm < 0);
  if (direction != previousDirection) ledcWrite(MOTOR_PWM_PIN, 0);
  previousDirection = direction;
  digitalWrite(MOTOR_IN1_PIN, pwm > 0 ? HIGH : LOW);
  digitalWrite(MOTOR_IN2_PIN, pwm < 0 ? HIGH : LOW);
  ledcWrite(MOTOR_PWM_PIN, abs(pwm));
}

void writeLights(bool redEnabled) {
  if (redOutputOn != redEnabled) {
    redOutputOn = redEnabled;
    diagnosticLed(LIGHT_RED_PIN, redEnabled, lightStrobeActive ? "MOTOR_STROBE" : "MOTOR_STOP");
  }
}

void updateLights(uint32_t now) {
  updateServoStrobe(millis());
  if (localAppliedPwm == 0) {
    lightStrobeActive = false;
    writeLights(false);
    diagnosticLed(LIGHT_GREEN_PIN, !(actionBlinkUntil && int32_t(now - actionBlinkUntil) < 0), actionBlinkUntil && int32_t(now - actionBlinkUntil) < 0 ? actionBlinkReason : "IDLE");
    return;
  }
  diagnosticLed(LIGHT_GREEN_PIN, actionBlinkUntil && int32_t(now - actionBlinkUntil) < 0, actionBlinkUntil && int32_t(now - actionBlinkUntil) < 0 ? actionBlinkReason : "MOTOR_ACTIVE");
  if (!lightStrobeActive) {
    lightStrobeActive = true;
    lastLightToggleAt = now;
    writeLights(true);
    return;
  }
  if (now - lastLightToggleAt >= LIGHT_BLINK_HALF_PERIOD_MS) {
    lastLightToggleAt = now;
    writeLights(!redOutputOn);
  }
}

void updateLocalMotor() {
  const uint32_t now = millis();
  const int16_t targetPwm = carriages[0].outputPwm;
  if (targetPwm == 0 && localAppliedPwm != 0) {
    localAppliedPwm = 0;
    lastLocalRampAt = now;
    writeLocalMotor(0);
    return;
  }
  if (localAppliedPwm == targetPwm) return;
  if (now - lastLocalRampAt < RAMP_INTERVAL_MS) return;
  lastLocalRampAt = now;
  if ((localAppliedPwm > 0 && targetPwm < 0) || (localAppliedPwm < 0 && targetPwm > 0)) {
    localAppliedPwm = 0;
  } else if (localAppliedPwm < targetPwm) {
    localAppliedPwm += RAMP_STEP;
    if (localAppliedPwm > targetPwm) localAppliedPwm = targetPwm;
  } else if (localAppliedPwm > targetPwm) {
    localAppliedPwm -= RAMP_STEP;
    if (localAppliedPwm < targetPwm) localAppliedPwm = targetPwm;
  }
  writeLocalMotor(localAppliedPwm);
}

void writeLocalServo(uint8_t physicalAngle) {
  const uint8_t commandAngle = constrain(uint16_t(physicalAngle) * 2, uint16_t(0), uint16_t(180));
  const uint16_t pulseUs = SERVO_MIN_PULSE_US +
      (uint32_t(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * commandAngle) / 180;
  const uint32_t duty = (uint64_t(pulseUs) * SERVO_FREQUENCY_HZ * 16384ULL + 500000ULL) / 1000000ULL;
  if (!diagnosticServo(physicalAngle, pulseUs)) return;
  const bool written = ledcWrite(SERVO_SIGNAL_PIN, duty);
  setServoStrobe(written ? commandAngle : 0);
}
