#pragma once
#include <Arduino.h>
#include <stdarg.h>

// Set to 0 at build time to remove diagnostics. Messages are dropped when
// the serial TX buffer is full; motor and radio tasks must never wait for it.
#ifndef CARRIAGE_DEBUG_LOG
#define CARRIAGE_DEBUG_LOG 1
#endif
static void eventLog(const char *component, const char *action, const char *format = "", ...) {
#if CARRIAGE_DEBUG_LOG
  char line[200];
  int n = snprintf(line, sizeof(line), "[%010lu ms] %s | %s", (unsigned long)millis(), component, action);
  if (n < 0 || n >= int(sizeof(line) - 3)) return;
  if (format[0]) {
    n += snprintf(line + n, sizeof(line) - n, " | ");
    va_list args; va_start(args, format);
    int added = vsnprintf(line + n, sizeof(line) - n, format, args);
    va_end(args);
    if (added < 0) return;
    n += added;
  }
  if (n >= int(sizeof(line) - 2)) n = sizeof(line) - 3;
  line[n++] = '\n';
  if (Serial.availableForWrite() >= n) Serial.write((const uint8_t *)line, n);
#endif
}

static uint32_t actionBlinkUntil = 0;
static const char *actionBlinkReason = "none";
static void requestActionBlink(const char *reason) {
  actionBlinkReason = reason;
  actionBlinkUntil = millis() + 100;
}
static void diagnosticMotor(int16_t pwm) {
  static int16_t previous = 0;
  static uint32_t started = 0;
  static int8_t lastDirection = 0;
  if (pwm == previous) return;
  uint32_t now = millis();
  if (!previous && pwm) {
    started = now;
    const int8_t direction = pwm > 0 ? 1 : -1;
    if (lastDirection && lastDirection != direction)
      eventLog("MOTOR_GROUP", "DIRECTION_CHANGE", "direction=%s", direction > 0 ? "RIGHT" : "LEFT");
    lastDirection = direction;
    eventLog("MOTOR_GROUP", "START", "direction=%s", pwm > 0 ? "RIGHT" : "LEFT");
    requestActionBlink("MOTOR_START");
  } else if (previous && !pwm) {
    eventLog("MOTOR_GROUP", "STOP", "duration=%lu ms", (unsigned long)(now - started));
    requestActionBlink("MOTOR_STOP");
  } else if ((previous < 0) != (pwm < 0)) {
    eventLog("MOTOR_GROUP", "DIRECTION_CHANGE", "direction=%s", pwm > 0 ? "RIGHT" : "LEFT");
    requestActionBlink("DIRECTION_CHANGE");
  }
  eventLog("MOTOR_GROUP", "PWM_CHANGE", "old=%d new=%d max=%u frequency=%u Hz RPM=unmeasured", previous, pwm, PWM_MAX, PWM_FREQUENCY_HZ);
  previous = pwm;
}
static bool diagnosticServo(uint8_t angle, uint32_t pulseUs) {
  static int previous = -1;
  if (previous == angle) return false;
  uint32_t now = millis();
  eventLog("SERVO", angle ? "POSITION_COMMAND" : "CLOSE_COMMAND", "previous=%d target=%u deg pulse=%lu us feedback=none", previous, angle, (unsigned long)pulseUs);
  eventLog("SERVO", "COMMAND_ISSUED", "elapsed=%lu ms next=controlled by program", (unsigned long)(millis()-now));
  if (previous >= 0) requestActionBlink(angle ? "SERVO_OPEN" : "SERVO_CLOSE");
  previous = angle;
  return true;
}
static void diagnosticLed(uint8_t pin, bool on, const char *reason) {
  static bool red = false, green = false;
  static bool redKnown = false, greenKnown = false;
  static uint32_t redSince = 0, greenSince = 0;
  bool &state = pin == LIGHT_RED_PIN ? red : green;
  bool &known = pin == LIGHT_RED_PIN ? redKnown : greenKnown;
  uint32_t &since = pin == LIGHT_RED_PIN ? redSince : greenSince;
  if (known && state == on) return;
  const uint32_t now = millis();
  digitalWrite(pin, on ? HIGH : LOW);
  eventLog(pin == LIGHT_RED_PIN ? "LED_R" : "LED_G", on ? "ON" : "OFF", "reason=%s previous_duration=%lu ms", reason, (unsigned long)(known ? now - since : 0));
  state = on; known = true; since = now;
}
