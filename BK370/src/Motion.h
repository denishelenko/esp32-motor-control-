#pragma once
#include "Hardware.h"
#include "EncoderCalibration.h"

namespace bk370 {
enum class Direction : int8_t { Left = -1, Right = 1 };
enum class Mode : uint8_t { Idle, Target, Program, Jog, Timed };
enum class Op : uint8_t { Stop, Speed, Target, Program, Servo, Jog, Timed, Zero, Reset, Start, Idle, Abort };
struct Command {
  Op op = Op::Idle;
  int8_t direction = 0;
  uint8_t speed = 50, value = 0;
  uint32_t duration = 0;
  float metres = 0;
};

class MotorGroup {
 public:
  int64_t count[2]{};
  uint8_t speed = 50, program = 1;
  Mode mode = Mode::Idle;
  float position = 0, target = 0;
  int16_t applied[2]{};
  bool centered = false, fault = false, encodersReady = false;

  void begin() {
    bool ready = true;
    encodersReady = true;
    for (unsigned i = 0; i < 2; ++i) {
      const auto &cfg = Motors[i];
      pinMode(cfg.pwmA, OUTPUT);
      pinMode(cfg.pwmB, OUTPUT);
      digitalWrite(cfg.pwmA, LOW);
      digitalWrite(cfg.pwmB, LOW);
      // MDD3A requires one PWM input per direction. Startup is LOW/LOW.
      const bool a = ledcAttachChannel(cfg.pwmA, 1000, 10, i * 2);
      const bool b = ledcAttachChannel(cfg.pwmB, 1000, 10, i * 2 + 1);
      pwmReady_[i] = a && b;
      if (!pwmReady_[i]) {
        if (a) ledcDetach(cfg.pwmA);
        if (b) ledcDetach(cfg.pwmB);
        pinMode(cfg.pwmA, OUTPUT);
        pinMode(cfg.pwmB, OUTPUT);
        digitalWrite(cfg.pwmA, LOW);
        digitalWrite(cfg.pwmB, LOW);
      }
      ready &= pwmReady_[i];
      encodersReady &= encoder_[i].begin(cfg);
    }
    hardwareReady_ = encodersReady && ready;
    log("MOTOR", "output=%d encoders=%d commissioned=%d", ready, encodersReady, DriveCommissioned);
  }
  bool calibrated() const {
    return Motors[0].countsPerMeter > 0 && Motors[1].countsPerMeter > 0;
  }
  bool zero(uint32_t now) {
    if (!encodersReady || mode != Mode::Idle || now - lastEncoderChange_ < 350) return false;
    for (unsigned i = 0; i < 2; ++i) origin_[i] = count[i];
    centered = true;
    position = target = 0;
    log("ENCODER", "center established");
    return true;
  }
  void setSpeed(uint8_t percent) {
    percent = constrain(percent, 1, 100);
    if (speed != percent) log("MOTOR", "speed=%u -> %u%%", speed, percent);
    speed = percent;
  }
  bool moveFor(Direction direction, uint32_t ms, uint32_t now) {
    if (!ms || ms > 10000 || !validDirection(direction)) return false;
    return start(Mode::Timed, direction, now, ms);
  }
  bool moveDistanceM(Direction direction, float metres, uint32_t now) {
    if (!validDirection(direction) || !isfinite(metres) || metres <= 0) return false;
    return moveTo(position + int8_t(direction) * metres, now);
  }
  bool moveDistanceCm(Direction direction, float cm, uint32_t now) {
    return moveDistanceM(direction, cm / 100.0f, now);
  }
  bool moveTo(float metres, uint32_t now) {
    return beginTarget(metres, Mode::Target, now);
  }
  bool startProgram(uint8_t id, uint32_t now) {
    if (id < 1 || id > 4) return false;
    program = id;
    const float first = id == 1 && position < LimitM - 0.1f ? LimitM : nextTarget();
    return beginTarget(first, Mode::Program, now);
  }
  bool jog(Direction direction, uint32_t now) {
    if (!validDirection(direction)) return false;
    if (mode != Mode::Jog || direction_ != direction) {
      if (!start(Mode::Jog, direction, now, 650)) return false;
      target = direction == Direction::Right ? LimitM : -LimitM;
    }
    jogAt_ = now;
    return true;
  }
  uint32_t remainingMs(uint32_t now) const {
    return mode == Mode::Timed && now - started_ < duration_ ? duration_ - (now - started_) : 0;
  }
  void stop(uint32_t now, const char *reason) {
    if (mode != Mode::Idle)
      log("MOTOR", "end=%s elapsed=%lu ms position=%.3f m", reason, (unsigned long)(now - started_), position);
    if (applied[0] || applied[1]) lastPowered_ = now;
    mode = Mode::Idle;
    waiting_ = false;
    for (unsigned i = 0; i < 2; ++i) write(i, 0, now);
    target = position;
  }
  void tick(uint32_t now, bool permit) {
    permitted_ = permit;
    for (unsigned i = 0; i < 2; ++i) {
      int64_t current;
      if (!encoder_[i].read(current)) {
        encodersReady = false;
        fault = true;
        stop(now, "encoder read error");
        return;
      }
      if (count[i] != current) lastEncoderChange_ = now;
      count[i] = current;
    }
    float wheel[2]{};
    if (centered && calibrated()) {
      for (unsigned i = 0; i < 2; ++i)
        EncoderCalibration{Motors[i].countsPerMeter}.distanceMeters(count[i] - origin_[i], wheel[i]);
      position = (wheel[0] + wheel[1]) * 0.5f;
    }
    if (!permit || fault) { stop(now, "interlock"); return; }
    if (mode == Mode::Idle) return;
    const uint32_t elapsed = now - started_;
    if ((mode == Mode::Timed && elapsed >= duration_) || (mode == Mode::Jog && now - jogAt_ >= 650) ||
        (mode != Mode::Timed && elapsed >= 30000 && !waiting_)) {
      stop(now, "time limit");
      return;
    }
    const int sign = int8_t(direction_);
    if (centered && calibrated() && (sign * wheel[0] >= LimitM || sign * wheel[1] >= LimitM) &&
        (mode == Mode::Timed || mode == Mode::Jog)) { stop(now, "track limit"); return; }

    const float remaining = sign * (target - position);
    if (mode == Mode::Target || mode == Mode::Program) {
      if (waiting_) {
        if (now - waitAt_ >= (program == 2 ? 350U : program == 3 ? 150U : 500U)) {
          if (!beginTarget(nextTarget(), Mode::Program, now)) stop(now, "program ended");
        }
        return;
      }
      if (remaining <= 0.025f) {
        if (mode == Mode::Target) { stop(now, "target reached"); return; }
        for (unsigned i = 0; i < 2; ++i) write(i, 0, now);
        log("MOTOR", "program segment ended at %.3f m", position);
        waiting_ = true;
        waitAt_ = lastPowered_ = now;
        return;
      }
      if (sign * wheel[0] >= LimitM || sign * wheel[1] >= LimitM) {
        stop(now, "track limit");
        return;
      }
    }
    if (elapsed >= 800 && now - checkedAt_ >= 300) {
      bool bad = false;
      for (unsigned i = 0; i < 2; ++i) {
        const auto progress = sign * (count[i] - previous_[i]);
        bad |= progress < 0 || (applied[i] != 0 && progress == 0);
        previous_[i] = count[i];
      }
      checkedAt_ = now;
      if (bad) { trip(now, "encoder stalled or reversed"); return; }
    }
    if (centered && calibrated() && fabsf(wheel[0] - wheel[1]) > 0.5f) {
      trip(now, "encoders disagree");
      return;
    }
    if (now - lastPowered_ < 100) return;
    if (now - lastUpdate_ < 10) return;
    lastUpdate_ = now;
    float scale = min(1.0f, (elapsed + 10) / float(RampUpMs));
    if (mode == Mode::Timed && RampDownMs) scale = min(scale, (duration_ - elapsed) / float(RampDownMs));
    if ((mode == Mode::Target || mode == Mode::Program) && SlowZoneM > 0)
      scale = min(scale, constrain(remaining / SlowZoneM, 0.22f, 1.0f));
    const int requested = speed;
    float progressDifference = 0;
    if (calibrated()) {
      const float first = (count[0] - segmentOrigin_[0]) / Motors[0].countsPerMeter;
      const float second = (count[1] - segmentOrigin_[1]) / Motors[1].countsPerMeter;
      progressDifference = sign * (first - second);
    }
    for (unsigned i = 0; i < 2; ++i) {
      // Reduce output on the leading wheel without increasing the other wheel's duty.
      const float lead = i == 0 ? progressDifference : -progressDifference;
      const float correction = constrain(lead * 2.0f, 0.0f, 0.18f);
      const float duty = scale * 1023.0f * requested / 100.0f * Motors[i].dutyMultiplier;
      write(i, sign * int(constrain(duty * (1.0f - correction), 0.0f, 1023.0f)), now);
    }
    if (now - telemetryAt_ >= 500) {
      telemetryAt_ = now;
      log("MOTOR", "PWM=%d/%d enc=%lld/%lld", applied[0], applied[1], (long long)count[0], (long long)count[1]);
    }
  }
 private:
  bool validDirection(Direction d) const { return d == Direction::Left || d == Direction::Right; }
  void trip(uint32_t now, const char *reason) {
    fault = true;
    log("FAULT", "%s", reason);
    stop(now, reason);
  }
  bool start(Mode modeWanted, Direction direction, uint32_t now, uint32_t duration = 0) {
    if (!hardwareReady_ || !DriveCommissioned || !permitted_ || fault) return false;
    if (mode != Mode::Idle && !waiting_ && direction == direction_ && mode == Mode::Target && modeWanted == Mode::Target) return true;
    if (mode != Mode::Idle && !waiting_) stop(now, "replaced");
    mode = modeWanted;
    direction_ = direction;
    started_ = checkedAt_ = now;
    duration_ = duration;
    waiting_ = false;
    for (unsigned i = 0; i < 2; ++i) {
      previous_[i] = count[i];
      segmentOrigin_[i] = count[i];
    }
    log("MOTOR", "start mode=%u dir=%d speed=%u%% duration=%lu ms", unsigned(mode), int(direction), speed, (unsigned long)duration);
    return true;
  }
  bool beginTarget(float metres, Mode m, uint32_t now) {
    if (!centered || !calibrated() || !isfinite(metres) || metres < -LimitM || metres > LimitM) return false;
    if (fabsf(metres - position) <= 0.025f) {
      stop(now, "already at target");
      return true;
    }
    if (!start(m, metres > position ? Direction::Right : Direction::Left, now)) return false;
    target = metres;
    return true;
  }
  float nextTarget() const {
    if (program == 1) return position < LimitM - 0.1f && direction_ == Direction::Left ? LimitM :
      (position <= -LimitM + 0.1f ? LimitM : -LimitM);
    if (program == 2) {
      bool right = position <= -LimitM + 0.1f || (position < LimitM - 0.1f && direction_ == Direction::Right);
      return constrain(2.0f * (right ? floorf((position + 0.05f) / 2) + 1 : ceilf((position - 0.05f) / 2) - 1), -LimitM, LimitM);
    }
    const float distance = program == 3 ? random(10, 31) / 10.0f : random(40, 71) / 10.0f;
    float step = random(2) ? distance : -distance;
    if (fabsf(position + step) > LimitM) step = -step;
    return constrain(position + step, -LimitM, LimitM);
  }
  void write(unsigned i, int val, uint32_t now) {
    if (!pwmReady_[i] || val == applied[i]) return;
    const auto &cfg = Motors[i];
    const int physical = cfg.reverseMotor ? -val : val;
    // Clear the opposite input before applying duty to the selected input.
    if (physical > 0) {
      ledcWrite(cfg.pwmB, 0);
      ledcWrite(cfg.pwmA, physical);
    } else if (physical < 0) {
      ledcWrite(cfg.pwmA, 0);
      ledcWrite(cfg.pwmB, -physical);
    } else {
      ledcWrite(cfg.pwmA, 0);
      ledcWrite(cfg.pwmB, 0);
    }
    if ((!val) != (!applied[i])) log("PWM", "motor=%u duty=%d/1023 time=%lu", i + 1, val, (unsigned long)now);
    applied[i] = val;
  }
  Encoder encoder_[2];
  int64_t origin_[2]{}, previous_[2]{}, segmentOrigin_[2]{};
  bool hardwareReady_ = false, permitted_ = false, waiting_ = false, pwmReady_[2]{};
  Direction direction_ = Direction::Right;
  uint32_t started_ = 0, duration_ = 0, lastEncoderChange_ = 0, checkedAt_ = 0, jogAt_ = 0;
  uint32_t lastPowered_ = 0, lastUpdate_ = 0, waitAt_ = 0, telemetryAt_ = 0;
};

struct Status {
  float position = 0, target = 0;
  int32_t count[2]{};
  int16_t pwm[2]{};
  uint16_t mv = 0;
  float amps = 0;
  bool currentValid = false;
  uint32_t remainingMs = 0;
  Mode mode = Mode::Idle;
  uint8_t speed = 50, program = 1, servoTarget = 0, servoSelected = 45;
  bool ready = false, locked = false, centered = false, calibrated = false, fault = false, servo = false;
};

class Carriage {
 public:
  void begin() {
    motors.begin(); battery.begin(); servo.begin();
    pinMode(RedPin, OUTPUT);
    pinMode(GreenPin, OUTPUT);
  }
  void stop(uint32_t now, const char *reason) { motors.stop(now, reason); servo.off(); }
  bool execute(const Command &cmd, uint32_t now) {
    auto direction = static_cast<Direction>(cmd.direction);
    if (cmd.op == Op::Abort) { stop(now, "safety stop"); return true; }
    if (cmd.op == Op::Stop) { motors.stop(now, "command"); flash(now); return true; }
    if (cmd.op == Op::Zero) return motors.zero(now);
    if (cmd.op == Op::Reset) return battery.reset(now);
    if (cmd.op == Op::Speed) { motors.setSpeed(cmd.speed); return true; }
    if (cmd.op == Op::Servo && cmd.value == 0) { servo.off(); flash(now); return true; }
    if (!DriveCommissioned || !battery.ready(now) || motors.fault) return false;
    switch (cmd.op) {
      case Op::Servo: {
        const bool accepted = cmd.duration ? servo.hold(cmd.value, cmd.duration, now) : servo.cycle(cmd.value, now);
        if (accepted) flash(now);
        return accepted;
      }
      case Op::Target: motors.setSpeed(cmd.speed); return motors.moveTo(cmd.metres, now);
      case Op::Jog: motors.setSpeed(cmd.speed); return motors.jog(direction, now);
      case Op::Timed: motors.setSpeed(cmd.speed); return motors.moveFor(direction, cmd.duration, now);
      case Op::Start: return startProgram(motors.program, now);
      case Op::Program: motors.setSpeed(cmd.speed); return startProgram(cmd.value, now);
      default: return false;
    }
  }
  void tick(uint32_t now, bool linkOk) {
    battery.tick(now);
    const bool permit = linkOk && battery.allowed(now);
    const auto previousAngle = servo.angle;
    const bool previousServo = servo.enabled;
    if (!permit) servo.off();
    motors.tick(now, permit);
    if (motors.fault) servo.off();
    servo.tick(now);
    const bool moving = motors.applied[0] || motors.applied[1];
    if (moving != moving_ || servo.angle != previousAngle || servo.enabled != previousServo) flash(now);
    moving_ = moving;
    if (flashActive_ && now - flashAt_ >= 100) flashActive_ = false;
    const bool red = moving ? (now / 125) % 2 == 0 : flashActive_;
    led(RedPin, red, red_);
    led(GreenPin, !moving && !red && permit && !motors.fault, green_);
  }
  Status status(uint32_t now) const {
    Status s;
    s.position = motors.position; s.target = motors.target;
    for (unsigned i = 0; i < 2; ++i) { s.count[i] = motors.count[i]; s.pwm[i] = motors.applied[i]; }
    s.mv = battery.valid ? battery.millivolts : 0;
    s.amps = battery.amps;
    s.currentValid = battery.currentValid;
    s.mode = motors.mode; s.speed = motors.speed; s.program = motors.program;
    s.remainingMs = motors.remainingMs(now);
    s.servo = servo.enabled; s.servoTarget = servo.angle; s.servoSelected = servo.selected;
    s.ready = DriveCommissioned && battery.ready(now) && motors.encodersReady && !motors.fault;
    s.locked = battery.locked; s.fault = motors.fault;
    s.centered = motors.centered; s.calibrated = motors.calibrated();
    return s;
  }
 private:
  void flash(uint32_t now) { flashAt_ = now; flashActive_ = true; }
  bool startProgram(uint8_t id, uint32_t now) {
    if (!motors.startProgram(id, now)) return false;
    if (!servo.enabled && !servo.cycle(servo.selected, now)) {
      motors.stop(now, "servo PWM error");
      return false;
    }
    return true;
  }
  void led(uint8_t pin, bool on, bool &old) {
    if (old == on) return;
    old = on;
    digitalWrite(pin, on);
    log(pin == RedPin ? "LED_R" : "LED_G", "%s", on ? "on" : "off");
  }
  MotorGroup motors;
  BatteryMonitor battery;
  ServoOutput servo;
  uint32_t flashAt_ = 0;
  bool red_ = false, green_ = false, flashActive_ = false, moving_ = false;
};
}
