#pragma once
#include "event_debug.h"

// ESP32-S3 motor, servo and indicator outputs.

void writeServoAngle(uint8_t physicalAngle) {
  physicalAngle = constrain(physicalAngle, SERVO_CLOSED_ANGLE, SERVO_PHYSICAL_MAX_ANGLE);
  const uint8_t commandAngle = physicalAngle * SERVO_COMMAND_PER_PHYSICAL_DEGREE;
  const uint32_t pulseUs = map(commandAngle, 0, 180, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
  const uint32_t duty = (uint64_t(pulseUs) * SERVO_FREQUENCY_HZ * 16384ULL + 500000ULL) / 1000000ULL;
  if (!diagnosticServo(physicalAngle, pulseUs)) return;
  ledcWrite(SERVO_SIGNAL_PIN, duty);
}

void writeLights(bool redEnabled) {
  if (redOutputOn != redEnabled) {
    redOutputOn = redEnabled;
    diagnosticLed(LIGHT_RED_PIN, redEnabled, lightStrobeActive ? "MOTOR_STROBE" : "MOTOR_STOP");
  }
}

void updateLights(uint32_t now) {
  if (appliedPwm == 0) {
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

void writeMotor(int16_t pwm) {
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

void updateMotor() {
  const uint32_t now = millis();
  if (targetPwm == 0 && appliedPwm != 0) {
    appliedPwm = 0;
    lastRampAt = now;
    writeMotor(0);
    return;
  }
  if (appliedPwm == targetPwm) return;
  if (now - lastRampAt < RAMP_INTERVAL_MS) return;
  lastRampAt = now;
  if ((appliedPwm > 0 && targetPwm < 0) || (appliedPwm < 0 && targetPwm > 0)) {
    appliedPwm = 0;
  } else if (appliedPwm < targetPwm) {
    appliedPwm += RAMP_STEP;
    if (appliedPwm > targetPwm) appliedPwm = targetPwm;
  } else if (appliedPwm > targetPwm) {
    appliedPwm -= RAMP_STEP;
    if (appliedPwm < targetPwm) appliedPwm = targetPwm;
  }
  writeMotor(appliedPwm);
}

void updateServo() {
  if (appliedServoPosition == targetServoPosition) return;
  appliedServoPosition = targetServoPosition;
  writeServoAngle(appliedServoPosition);
}
