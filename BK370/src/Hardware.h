#pragma once
#include <Arduino.h>
#include <driver/pulse_cnt.h>
#include "Settings.h"

namespace bk370 {
inline void log(const char *event, const char *format = "", ...) {
  char line[112];
  int n = snprintf(line, sizeof(line), "[%010lu] %s ", (unsigned long)millis(), event);
  if (n < 0 || n >= int(sizeof(line) - 2)) return;
  va_list args;
  va_start(args, format);
  vsnprintf(line + n, sizeof(line) - n - 1, format, args);
  va_end(args);
  size_t size = strlen(line);
  line[size++] = '\n';
  if (Serial.availableForWrite() >= int(size)) Serial.write((const uint8_t *)line, size);
}

class Encoder {
 public:
  bool begin(const MotorSettings &cfg) {
    pinMode(cfg.encoderA, INPUT);
    pinMode(cfg.encoderB, INPUT);
    pcnt_unit_config_t unit{};
    unit.low_limit = -30000;
    unit.high_limit = 30000;
    unit.flags.accum_count = true;
    if (pcnt_new_unit(&unit, &unit_) != ESP_OK) return false;
    pcnt_glitch_filter_config_t filter{};
    filter.max_glitch_ns = 1000;
    if (pcnt_unit_set_glitch_filter(unit_, &filter) != ESP_OK) return false;
    if (!channel(cfg.encoderA, cfg.encoderB, false) || !channel(cfg.encoderB, cfg.encoderA, true)) return false;
    if (pcnt_unit_add_watch_point(unit_, unit.low_limit) != ESP_OK ||
        pcnt_unit_add_watch_point(unit_, unit.high_limit) != ESP_OK ||
        pcnt_unit_enable(unit_) != ESP_OK ||
        pcnt_unit_clear_count(unit_) != ESP_OK ||
        pcnt_unit_start(unit_) != ESP_OK) return false;
    sign_ = cfg.reverseEncoder ? -1 : 1;
    return true;
  }
  bool read(int64_t &result) const {
    int value;
    if (!unit_ || pcnt_unit_get_count(unit_, &value) != ESP_OK) return false;
    result = int64_t(value) * sign_;
    return true;
  }
 private:
  bool channel(int edge, int level, bool swap) {
    pcnt_chan_config_t cfg{};
    cfg.edge_gpio_num = edge;
    cfg.level_gpio_num = level;
    pcnt_channel_handle_t handle;
    return pcnt_new_channel(unit_, &cfg, &handle) == ESP_OK &&
      pcnt_channel_set_edge_action(handle,
        swap ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        swap ? PCNT_CHANNEL_EDGE_ACTION_DECREASE : PCNT_CHANNEL_EDGE_ACTION_INCREASE) == ESP_OK &&
      pcnt_channel_set_level_action(handle, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE) == ESP_OK;
  }
  pcnt_unit_handle_t unit_ = nullptr;
  int8_t sign_ = 1;
};

class ServoOutput {
 public:
  uint8_t angle = 0, selected = 45;
  bool enabled = false;
  void begin() { pinMode(ServoPin, OUTPUT); digitalWrite(ServoPin, LOW); }
  void off() {
    if (attached_) {
      ledcDetach(ServoPin);
      pinMode(ServoPin, OUTPUT);
      digitalWrite(ServoPin, LOW);
      log("SERVO", "PWM disabled");
    }
    enabled = attached_ = cycling_ = false;
    travelMs_ = 0;
    angle = 0;
  }
  void close(uint32_t now) {
    if (attached_ && !write(0, now)) { off(); return; }
    enabled = cycling_ = false;
  }
  bool moving(uint32_t now) const { return travelMs_ && now - travelAt_ < travelMs_; }
  bool hold(uint8_t degrees, uint32_t durationMs, uint32_t now) {
    if (!write(degrees, now)) return false;
    cycling_ = false;
    holdMs_ = durationMs;
    changedAt_ = now;
    log("SERVO", "command=%u deg hold=%lu ms", degrees, (unsigned long)durationMs);
    return true;
  }
  bool cycle(uint8_t degrees, uint32_t now) {
    if (degrees != 45 && degrees != 90) return false;
    if (!write(enabled && angle ? degrees : 0, now)) return false;
    selected = degrees;
    cycling_ = true;
    changedAt_ = now;
    log("SERVO", "cycle=%u deg", degrees);
    return true;
  }
  void tick(uint32_t now) {
    if (travelMs_ && now - travelAt_ >= travelMs_) travelMs_ = 0;
    if (!enabled) return;
    if (!cycling_) {
      if (holdMs_ && now - changedAt_ >= holdMs_) off();
      return;
    }
    if (now - changedAt_ >= 2500) {
      const auto next = angle ? 0 : selected;
      if (!write(next, now)) { off(); return; }
      changedAt_ = now;
      log("SERVO", "target=%u deg next=2500 ms", angle);
    }
  }
 private:
  bool write(uint8_t degrees, uint32_t now) {
    if (degrees > 180) return false;
    constexpr uint32_t Hz = 333;
    if (!attached_) attached_ = ledcAttachChannel(ServoPin, Hz, 14, 4);
    if (!attached_) return false;
    const uint32_t us = 520 + (2000 * uint32_t(degrees)) / 180;
    if (!ledcWrite(ServoPin, (uint64_t(us) * Hz * 16384 + 500000) / 1000000)) return false;
    if (angle != degrees) {
      travelAt_ = now;
      travelMs_ = (abs(int(degrees) - int(angle)) * ServoTravel90Ms + 89) / 90;
    }
    enabled = true;
    angle = degrees;
    return true;
  }
  bool attached_ = false, cycling_ = false;
  uint32_t changedAt_ = 0, holdMs_ = 0;
  uint32_t travelAt_ = 0, travelMs_ = 0;
};
}
