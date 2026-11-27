#pragma once
#include <Arduino.h>

namespace bk370 {
constexpr size_t EventLineSize = 104;
constexpr size_t EventLogCapacity = 96;

class EventLog {
 public:
  void begin(uint8_t carriage) {
    carriage_ = carriage;
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#if ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(0);
#endif
#endif
  }

  uint8_t carriage() const { return carriage_; }
  uint32_t latest() const { return last_; }

  void append(const char *line) {
    char *slot = lines_[last_ % EventLogCapacity];
    snprintf(slot, EventLineSize, "%s", line);
    ++last_;
    print(slot);
  }

  void print(const char *line) {
    const auto len = strlen(line);
    writeSerial(Serial, line, len);
#if ARDUINO_USB_CDC_ON_BOOT
    writeSerial(Serial0, line, len);
#endif
  }

  uint32_t next(uint32_t after, const char *&line) const {
    if (!last_ || after >= last_) return 0;
    const uint32_t oldest = last_ >= EventLogCapacity ? last_ - EventLogCapacity + 1 : 1;
    const uint32_t id = max(after + 1, oldest);
    line = lines_[(id - 1) % EventLogCapacity];
    return id;
  }

 private:
  template<class Port>
  static void writeSerial(Port &port, const char *line, size_t len) {
    if (port.availableForWrite() >= int(len + 1)) {
      port.write(reinterpret_cast<const uint8_t *>(line), len);
      port.write(static_cast<uint8_t>('\n'));
    }
  }
  uint8_t carriage_ = 1;
  uint32_t last_ = 0;
  char lines_[EventLogCapacity][EventLineSize]{};
};

inline EventLog eventLog;

// Called on the firmware loop. Radio callbacks only enqueue packets.
inline void log(const char *event, const char *format = "", ...) {
  char line[EventLineSize];
  const int n = snprintf(line, sizeof(line), "[%010lu] BK%u %s ", (unsigned long)millis(), eventLog.carriage(), event);
  if (n < 0 || n >= int(sizeof(line))) return;
  va_list args;
  va_start(args, format);
  vsnprintf(line + n, sizeof(line) - n, format, args);
  va_end(args);
  if (!strcmp(event, "MOVE") || !strcmp(event, "SERVO")) eventLog.append(line);
  else eventLog.print(line);
}
}
