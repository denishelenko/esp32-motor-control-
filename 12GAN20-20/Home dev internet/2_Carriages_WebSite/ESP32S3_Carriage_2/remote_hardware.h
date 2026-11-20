#pragma once
#include "event_debug.h"

// ESP32-S3 motor, servo and indicator outputs.

static bool servoStrobeActive = false;
static uint32_t servoStrobeStartedAt = 0;
static uint32_t servoMotionStartedAt = 0, servoMotionMs = 0;

static void setServoStrobe(uint8_t commandAngle, bool written = true) {
  static uint8_t previousAngle = 0;
  if (!written) {
    servoMotionMs = 0;
  } else if (commandAngle != previousAngle) {
    const uint32_t angleDelta = abs(int(commandAngle) - int(previousAngle));
    servoMotionStartedAt = millis();
    servoMotionMs = (angleDelta * SERVO_TRAVEL_90_MS + 89) / 90;
    previousAngle = commandAngle;
  }

  const bool active = written && commandAngle > 0 && commandAngle < 180;
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

void writeServoAngle(uint8_t physicalAngle) {
  physicalAngle = constrain(physicalAngle, SERVO_CLOSED_ANGLE, SERVO_PHYSICAL_MAX_ANGLE);
  const uint8_t commandAngle = physicalAngle * SERVO_COMMAND_PER_PHYSICAL_DEGREE;
  const uint32_t pulseUs = map(commandAngle, 0, 180, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
  const uint32_t duty = (uint64_t(pulseUs) * SERVO_FREQUENCY_HZ * 16384ULL + 500000ULL) / 1000000ULL;
  if (!diagnosticServo(physicalAngle, pulseUs)) return;
  const bool written = ledcWrite(SERVO_SIGNAL_PIN, duty);
  setServoStrobe(commandAngle, written);
}

void writeLights(bool redEnabled) {
  if (redOutputOn != redEnabled) {
    redOutputOn = redEnabled;
    diagnosticLed(LIGHT_RED_PIN, redEnabled, lightStrobeActive ? "MOTION_STROBE" : "IDLE");
  }
}

void updateLights(uint32_t now) {
  updateServoStrobe(now);
  if (servoMotionMs && now - servoMotionStartedAt >= servoMotionMs) servoMotionMs = 0;
  const bool moving = appliedPwm != 0 || servoMotionMs != 0;
  diagnosticLed(LIGHT_GREEN_PIN, !moving, moving ? "MOTION" : "IDLE");
  if (!moving) {
    lightStrobeActive = false;
    writeLights(false);
    return;
  }
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
