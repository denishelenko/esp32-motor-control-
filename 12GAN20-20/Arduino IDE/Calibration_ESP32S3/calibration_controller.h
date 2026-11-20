#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>
#include "calibration_page.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error Select an ESP32-S3 board.
#endif

namespace calibration {
constexpr char Ssid[] = "Motor-Calibrate";
constexpr char Password[] = "motor8266";
constexpr int In1 = 1, In2 = 2, Pwm = 42;
constexpr bool ReverseDirection = true;
constexpr uint32_t MotorPeriodMs = 10, LeaseMs = 1000, RestMs = 100;
constexpr int RampStep = 24, MaxDuty = 1023;

enum class Phase : uint8_t { Idle, Queued, Running, Done, Stopped, LinkLost, Fault };
struct Run {
  Phase phase = Phase::Idle;
  uint32_t id = 0, duration = 0, startedAt = 0, elapsed = 0, leaseAt = 0;
  int direction = 0, speed = 0;
  bool stopRequested = false;
};

WebServer server(80);
SemaphoreHandle_t motorLock = nullptr;
Run run;
uint32_t stoppedAt = 0, stopEpoch = 0;
bool centerMarked = false;
bool hardwareReady = false;

bool active(Phase phase) { return phase == Phase::Queued || phase == Phase::Running; }

const char *phaseName(Phase phase) {
  switch (phase) {
    case Phase::Idle: return "idle";
    case Phase::Queued: return "queued";
    case Phase::Running: return "running";
    case Phase::Done: return "done";
    case Phase::Stopped: return "stopped";
    case Phase::LinkLost: return "link_lost";
    default: return "fault";
  }
}

void stopLocked(Phase reason, uint32_t now) {
  if (!ledcWrite(Pwm, 0)) {
    ledcDetach(Pwm);
    pinMode(Pwm, OUTPUT);
    digitalWrite(Pwm, LOW);
    hardwareReady = false;
    reason = Phase::Fault;
  }
  digitalWrite(In1, LOW);
  digitalWrite(In2, LOW);
  run.elapsed = run.phase == Phase::Running ? now - run.startedAt : 0;
  if (reason == Phase::Fault) hardwareReady = false;
  run.phase = reason;
  run.stopRequested = false;
  stoppedAt = now;
}

void serviceMotor(void *) {
  int duty = 0;
  TickType_t next = xTaskGetTickCount();
  for (;;) {
    xSemaphoreTake(motorLock, portMAX_DELAY);
    const uint32_t now = millis();
    if (active(run.phase)) {
      if (run.stopRequested) {
        stopLocked(Phase::Stopped, now);
      } else if (run.phase == Phase::Running && now - run.startedAt >= run.duration) {
        stopLocked(Phase::Done, now);
      } else if (now - run.leaseAt >= LeaseMs) {
        stopLocked(Phase::LinkLost, now);
      } else {
        if (run.phase == Phase::Queued) {
          const int physicalDirection = ReverseDirection ? -run.direction : run.direction;
          digitalWrite(In1, physicalDirection > 0 ? HIGH : LOW);
          digitalWrite(In2, physicalDirection < 0 ? HIGH : LOW);
          run.startedAt = millis();
          run.phase = Phase::Running;
          duty = 0;
        }
        const int requested = MaxDuty * run.speed / 100;
        if (duty < requested) {
          duty = min(duty + RampStep, requested);
          if (!ledcWrite(Pwm, duty)) stopLocked(Phase::Fault, millis());
        }
      }
    }
    xSemaphoreGive(motorLock);
    vTaskDelayUntil(&next, pdMS_TO_TICKS(MotorPeriodMs));
  }
}

bool number(const char *name, uint32_t &result) {
  const String arg = server.arg(name);
  if (arg.isEmpty() || arg.length() > 10) return false;
  uint64_t value = 0;
  for (char digit : arg) {
    if (digit < '0' || digit > '9') return false;
    value = value * 10 + digit - '0';
  }
  if (value > UINT32_MAX) return false;
  result = static_cast<uint32_t>(value);
  return true;
}

void reject(int code, const char *message) {
  server.send(code, "text/plain; charset=utf-8", message);
}

void sendState() {
  Run snapshot;
  bool center, ready;
  uint32_t epoch;
  xSemaphoreTake(motorLock, portMAX_DELAY);
  snapshot = run;
  if (snapshot.phase == Phase::Running) snapshot.elapsed = millis() - snapshot.startedAt;
  center = centerMarked;
  ready = hardwareReady;
  epoch = stopEpoch;
  xSemaphoreGive(motorLock);

  char body[270];
  snprintf(body, sizeof(body),
           "{\"phase\":\"%s\",\"id\":%lu,\"durationMs\":%lu,\"elapsedMs\":%lu,"
           "\"direction\":%d,\"speed\":%d,\"center\":%s,\"ready\":%s,\"stopEpoch\":%lu}",
           phaseName(snapshot.phase), static_cast<unsigned long>(snapshot.id),
           static_cast<unsigned long>(snapshot.duration), static_cast<unsigned long>(snapshot.elapsed),
           snapshot.direction, snapshot.speed, center ? "true" : "false", ready ? "true" : "false",
           static_cast<unsigned long>(epoch));
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void start() {
  uint32_t duration, speed, epoch;
  const String direction = server.arg("direction");
  if ((direction != "left" && direction != "right") || !number("ms", duration) ||
      duration < 200 || duration > 10000 || !number("speed", speed) || speed < 1 || speed > 100 ||
      !number("stopEpoch", epoch)) {
    reject(400, "Некоректний напрямок, час або ШІМ.");
    return;
  }
  const uint32_t id = esp_random() | 1;
  xSemaphoreTake(motorLock, portMAX_DELAY);
  const uint32_t now = millis();
  const bool allowed = hardwareReady && !active(run.phase) && now - stoppedAt >= RestMs && epoch == stopEpoch;
  if (allowed) {
    run = Run{};
    run.phase = Phase::Queued;
    run.id = id;
    run.direction = direction == "left" ? -1 : 1;
    run.speed = static_cast<int>(speed);
    run.duration = duration;
    run.leaseAt = now;
  }
  xSemaphoreGive(motorLock);
  if (allowed) sendState();
  else reject(409, "Дочекайся зупинки або перевір драйвер.");
}

void heartbeat() {
  uint32_t id;
  if (!number("id", id) || !id) { reject(400, "Потрібен номер виміру."); return; }
  xSemaphoreTake(motorLock, portMAX_DELAY);
  const uint32_t now = millis();
  if (id == run.id && active(run.phase) && now - run.leaseAt < LeaseMs) run.leaseAt = now;
  xSemaphoreGive(motorLock);
  sendState();
}

void requestStop() {
  xSemaphoreTake(motorLock, portMAX_DELAY);
  ++stopEpoch;
  if (active(run.phase)) run.stopRequested = true;
  xSemaphoreGive(motorLock);
  sendState();
}

void markCenter() {
  xSemaphoreTake(motorLock, portMAX_DELAY);
  const bool allowed = !active(run.phase) && millis() - stoppedAt >= RestMs;
  if (allowed) centerMarked = true;
  xSemaphoreGive(motorLock);
  if (allowed) sendState();
  else reject(409, "Спочатку зупини каретку.");
}

void begin() {
  Serial.begin(115200);
  for (const int pin : {In1, In2, Pwm}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  motorLock = xSemaphoreCreateMutex();
  if (!motorLock) return;
  hardwareReady = ledcAttachChannel(Pwm, 1000, 10, 0);
  if (hardwareReady && !ledcWrite(Pwm, 0)) stopLocked(Phase::Fault, millis());
  if (xTaskCreate(serviceMotor, "calibrationMotor", 3072, nullptr, 3, nullptr) != pdPASS) {
    stopLocked(Phase::Fault, millis());
  }
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(Ssid, Password)) {
    Serial.println("Не вдалося запустити точку доступу Motor-Calibrate.");
    return;
  }
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", CalibrationPage); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/run", HTTP_POST, start);
  server.on("/api/heartbeat", HTTP_POST, heartbeat);
  server.on("/api/stop", HTTP_POST, requestStop);
  server.on("/api/center", HTTP_POST, markCenter);
  server.begin();
  Serial.println("Motor-Calibrate · http://192.168.4.1/");
}
}
