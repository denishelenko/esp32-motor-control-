#pragma once
#include "Hardware.h"

namespace bk370 {
enum class Direction : int8_t { Left = -1, Right = 1 };
enum class Mode : uint8_t { Idle, Target, Program, Jog, Timed };
enum class Op : uint8_t { Stop, Speed, Target, Program, Servo, Jog, Timed, Zero, Reset, Start, Idle, Abort, MotorStop, LightTest };
enum class LightTest : uint8_t { Off, Red, Green, Strobe };
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
  float countsPerSecond[2]{};
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
      if (pwmReady_[i]) pwmReady_[i] = ledcWrite(cfg.pwmA, 0) && ledcWrite(cfg.pwmB, 0);
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
    return CountsPerMeter[0] > 0 && CountsPerMeter[1] > 0;
  }
  bool speedCalibrated() const {
    return ConfiguredMaxSpeedMps > 0;
  }
  float maxSpeedMps() const {
    return speedCalibrated() ? ConfiguredMaxSpeedMps : 0;
  }
  bool zero(uint32_t now) {
    if (!encodersReady || mode != Mode::Idle || now - lastEncoderChange_ < 350) return false;
    for (unsigned i = 0; i < 2; ++i) origin_[i] = count[i];
    centered = true;
    position = target = 0;
    log("ENCODER", "center established");
    return true;
  }
  void setSpeed(uint8_t percent, uint32_t now) {
    percent = min(percent, uint8_t(100));
    if (speed != percent) log("MOTOR", "speed=%u -> %u%%", speed, percent);
    speed = percent;
    if (!speed) stop(now, "zero speed · brake");
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
    finalPhase_ = FinalPhase::Drive;
    for (unsigned i = 0; i < 2; ++i) { integral_[i] = 0; write(i, 0, now); }
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
    const uint32_t sampleMs = now - sampledAt_;
    const bool freshSpeed = sampleMs >= 100;
    if (freshSpeed) {
      for (unsigned i = 0; i < 2; ++i) {
        countsPerSecond[i] = float(count[i] - sampledCounts_[i]) * 1000.0f / sampleMs;
        sampledCounts_[i] = count[i];
      }
      sampledAt_ = now;
    }
    float wheel[2]{};
    if (centered && calibrated()) {
      for (unsigned i = 0; i < 2; ++i)
        wheel[i] = float(count[i] - origin_[i]) * MetersPerCount[i];
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
    if (centered && calibrated() && fabsf(wheel[0] - wheel[1]) > 0.5f) {
      trip(now, "encoders disagree");
      return;
    }
    const int sign = int8_t(direction_);
    if (centered && calibrated() && (sign * wheel[0] >= LimitM || sign * wheel[1] >= LimitM) &&
        (mode == Mode::Timed || mode == Mode::Jog)) { stop(now, "track limit"); return; }

    const float remaining = sign * (target - position);
    const bool positioning = mode == Mode::Target || mode == Mode::Program;
    if (positioning) {
      if (waiting_) {
        if (now - waitAt_ >= (program == 2 ? 350U : program == 3 ? 150U : 500U)) {
          if (!beginTarget(nextTarget(), Mode::Program, now)) stop(now, "program ended");
        }
        return;
      }
      if (sign * wheel[0] >= LimitM || sign * wheel[1] >= LimitM) {
        stop(now, "track limit");
        return;
      }
      if (serviceFinalApproach(remaining, now)) return;
    }
    const bool creeping = finalPhase_ == FinalPhase::Creep;
    if (elapsed >= 800 && (!creeping || now - brakeAt_ >= 800) && now - checkedAt_ >= 300) {
      bool bad = false;
      for (unsigned i = 0; i < 2; ++i) {
        const auto progress = sign * (count[i] - previous_[i]);
        bad |= progress < 0 || (applied[i] != 0 && progress == 0);
        previous_[i] = count[i];
      }
      checkedAt_ = now;
      if (bad) { trip(now, "encoder stalled or reversed"); return; }
    }
    if (now - lastPowered_ < 100) return;
    if (now - lastUpdate_ < 10) return;
    lastUpdate_ = now;
    float scale = min(1.0f, (elapsed + 10) / float(RampUpMs));
    if (mode == Mode::Timed && RampDownMs) scale = min(scale, (duration_ - elapsed) / float(RampDownMs));
    const bool approaching = positioning && remaining <= SlowZoneM;
    if (approaching) {
      const float fractionLeft = (remaining - TargetToleranceM) / (SlowZoneM - TargetToleranceM);
      scale = min(scale, ApproachMinScale + (1.0f - ApproachMinScale) * fractionLeft);
    }
    const float requested = scale * ((mode == Mode::Program && program == 3) ? 100 : speed) / 100.0f;
    const float targetMps = creeping ? min(requested * maxSpeedMps(), FinalCreepMps) : requested * maxSpeedMps();
    float progressDifference = 0;
    if (calibrated()) {
      const float first = float(count[0] - segmentOrigin_[0]) * MetersPerCount[0];
      const float second = float(count[1] - segmentOrigin_[1]) * MetersPerCount[1];
      progressDifference = sign * (first - second);
    }
    for (unsigned i = 0; i < 2; ++i) {
      // Reduce output on the leading wheel without increasing the other wheel's duty.
      const float lead = i == 0 ? progressDifference : -progressDifference;
      const float correction = constrain(lead * 2.0f, 0.0f, 0.18f);
      const auto &cfg = Motors[i];
      const float allowedDuty = constrain(cfg.dutyMultiplier, 0.0f, 1.0f);
      const float ceiling = creeping ? min(allowedDuty, FinalCreepDutyLimit) : allowedDuty;
      float fraction = requested * ceiling;
      if (speedCalibrated()) {
        const float wheelMps = sign * countsPerSecond[i] * MetersPerCount[i];
        if (approaching && wheelMps > targetMps + ApproachBrakeMarginMps) {
          integral_[i] = 0;
          write(i, 0, now);
          if (fault) { stop(now, "motor PWM error"); return; }
          continue;
        }
        const float feedForward = targetMps * CountsPerMeter[i] / cfg.maxCountsPerSecond;
        const float measured = sign * countsPerSecond[i] / cfg.maxCountsPerSecond;
        const float error = feedForward - measured;
        const float baseDuty = feedForward * allowedDuty;
        const float next = baseDuty + 0.55f * error + integral_[i];
        if (freshSpeed && ((next > 0 && next < ceiling) || (next >= ceiling && error < 0) || (next <= 0 && error > 0)))
          integral_[i] = constrain(integral_[i] + 0.8f * error * sampleMs / 1000.0f, -0.5f, 0.5f);
        fraction = constrain(baseDuty + 0.55f * error + integral_[i], 0.0f, ceiling);
      }
      const float duty = fraction * 1023.0f;
      write(i, sign * int(constrain(duty * (1.0f - correction), 0.0f, 1023.0f)), now);
      if (fault) { stop(now, "motor PWM error"); return; }
    }
    if (now - telemetryAt_ >= 500) {
      telemetryAt_ = now;
      log("MOTOR", "PWM=%d/%d enc=%lld/%lld", applied[0], applied[1], (long long)count[0], (long long)count[1]);
    }
  }
 private:
  enum class FinalPhase : uint8_t { Drive, Brake, Creep };
  bool serviceFinalApproach(float remaining, uint32_t now) {
    if (finalPhase_ == FinalPhase::Drive && remaining > FinalBrakeM) return false;
    if (finalPhase_ == FinalPhase::Drive) finalStartedAt_ = now;
    if (now - finalStartedAt_ >= FinalApproachTimeoutMs) {
      stop(now, "braking or creep timed out");
      return true;
    }
    if (finalPhase_ == FinalPhase::Drive || (finalPhase_ == FinalPhase::Creep && remaining <= TargetToleranceM)) {
      finalPhase_ = FinalPhase::Brake;
      lastPowered_ = brakeAt_ = now;
      for (unsigned i = 0; i < 2; ++i) { integral_[i] = 0; write(i, 0, now); }
      log("BRAKE", "LOW/LOW remaining=%.3f m", remaining);
      if (fault) stop(now, "motor PWM error");
      return true;
    }
    if (finalPhase_ == FinalPhase::Creep) return false;
    if (now - brakeAt_ < BrakeQuietMs || now - lastEncoderChange_ < BrakeQuietMs) return true;
    if (remaining > TargetToleranceM) {
      finalPhase_ = FinalPhase::Creep;
      checkedAt_ = brakeAt_ = now;
      for (unsigned i = 0; i < 2; ++i) previous_[i] = count[i];
      log("BRAKE", "wheels stopped short; creep remaining=%.3f m", remaining);
      return true;
    }
    if (remaining < -TargetToleranceM || mode == Mode::Target) {
      stop(now, remaining < -TargetToleranceM ? "target overshot" : "at target, wheels stopped");
      return true;
    }
    log("MOTOR", "program segment ended at %.3f m, wheels stopped", position);
    waiting_ = true;
    waitAt_ = now;
    return true;
  }
  bool validDirection(Direction d) const { return d == Direction::Left || d == Direction::Right; }
  void trip(uint32_t now, const char *reason) {
    fault = true;
    log("FAULT", "%s", reason);
    stop(now, reason);
  }
  bool start(Mode modeWanted, Direction direction, uint32_t now, uint32_t duration = 0) {
    if (!hardwareReady_ || !DriveCommissioned || !permitted_ || fault || !speed ||
        (!speedCalibrated() && modeWanted != Mode::Timed)) return false;
    if (mode != Mode::Idle && !waiting_ && direction == direction_ && mode == Mode::Target && modeWanted == Mode::Target) return true;
    if (mode != Mode::Idle && !waiting_) stop(now, "replaced");
    mode = modeWanted;
    direction_ = direction;
    started_ = checkedAt_ = now;
    duration_ = duration;
    waiting_ = false;
    finalPhase_ = FinalPhase::Drive;
    for (unsigned i = 0; i < 2; ++i) {
      previous_[i] = count[i];
      segmentOrigin_[i] = count[i];
      integral_[i] = 0;
    }
    log("MOTOR", "start mode=%u dir=%d speed=%u%% duration=%lu ms", unsigned(mode), int(direction), speed, (unsigned long)duration);
    return true;
  }
  bool beginTarget(float metres, Mode m, uint32_t now) {
    if (!centered || !calibrated() || !isfinite(metres) || metres < -LimitM || metres > LimitM) return false;
    if (fabsf(metres - position) <= TargetToleranceM) {
      stop(now, "already at target");
      return true;
    }
    if (!start(m, metres > position ? Direction::Right : Direction::Left, now)) return false;
    if (fabsf(metres - target) > TargetToleranceM) finalPhase_ = FinalPhase::Drive;
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
    const bool ok = physical > 0 ? ledcWrite(cfg.pwmB, 0) && ledcWrite(cfg.pwmA, physical) :
      ledcWrite(cfg.pwmA, 0) && ledcWrite(cfg.pwmB, -physical);
    if (!ok) {
      for (uint8_t pin : {cfg.pwmA, cfg.pwmB}) {
        ledcDetach(pin);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
      }
      pwmReady_[i] = false;
      fault = true;
      applied[i] = 0;
      return;
    }
    if ((!val) != (!applied[i])) log("PWM", "motor=%u duty=%d/1023 time=%lu", i + 1, val, (unsigned long)now);
    applied[i] = val;
  }
  Encoder encoder_[2];
  int64_t origin_[2]{}, previous_[2]{}, segmentOrigin_[2]{};
  int64_t sampledCounts_[2]{};
  float integral_[2]{};
  bool hardwareReady_ = false, permitted_ = false, waiting_ = false, pwmReady_[2]{};
  Direction direction_ = Direction::Right;
  FinalPhase finalPhase_ = FinalPhase::Drive;
  uint32_t started_ = 0, duration_ = 0, lastEncoderChange_ = 0, checkedAt_ = 0, jogAt_ = 0;
  uint32_t lastPowered_ = 0, lastUpdate_ = 0, waitAt_ = 0, telemetryAt_ = 0;
  uint32_t sampledAt_ = 0;
  uint32_t finalStartedAt_ = 0, brakeAt_ = 0;
};

struct Status {
  float position = 0, target = 0;
  int32_t count[2]{};
  int16_t pwm[2]{};
  float countsPerSecond[2]{}, wheelMps[2]{}, maxMps = 0;
  uint16_t mv = 0;
  float amps = 0;
  bool currentValid = false;
  uint32_t remainingMs = 0;
  Mode mode = Mode::Idle;
  LightTest lightTest = LightTest::Off;
  uint8_t speed = 50, program = 1, servoTarget = 0, servoSelected = 45;
  bool ready = false, powered = false, locked = false, centered = false, calibrated = false, fault = false, servo = false;
};

class Carriage {
 public:
  void begin() {
    motors.begin(); battery.begin(); servo.begin();
    for (uint8_t pin : {RedPin, GreenPin, StrobePin}) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
  }
  void stop(uint32_t now, const char *reason) {
    motors.stop(now, reason);
    servo.off();
    test_ = LightTest::Off;
  }
  bool execute(const Command &cmd, uint32_t now) {
    auto direction = static_cast<Direction>(cmd.direction);
    if (cmd.op == Op::Abort) { stop(now, "safety stop"); return true; }
    if (cmd.op == Op::Stop || cmd.op == Op::MotorStop) {
      motors.stop(now, "command · brake LOW/LOW");
      test_ = LightTest::Off;
      if (cmd.op == Op::Stop) {
        if (battery.allowed(now)) servo.close(now);
        else servo.off();
      }
      return true;
    }
    if (cmd.op == Op::Zero) return motors.zero(now);
    if (cmd.op == Op::Reset) return battery.reset(now);
    if (cmd.op == Op::Speed) { motors.setSpeed(cmd.speed, now); return true; }
    if (cmd.op == Op::Servo && cmd.value == 0) {
      if (battery.allowed(now)) servo.close(now);
      else servo.off();
      return true;
    }
    if (!battery.ready(now) || motors.fault) return false;
    if (cmd.op == Op::LightTest) {
      if (motors.mode != Mode::Idle || servo.enabled || servo.moving(now) ||
          cmd.value < uint8_t(LightTest::Red) || cmd.value > uint8_t(LightTest::Strobe)) return false;
      test_ = static_cast<LightTest>(cmd.value);
      testAt_ = now;
      return true;
    }
    test_ = LightTest::Off;
    switch (cmd.op) {
      case Op::Servo: return cmd.duration ? servo.hold(cmd.value, cmd.duration, now) : servo.cycle(cmd.value, now);
      case Op::Target: motors.setSpeed(cmd.speed, now); return motors.moveTo(cmd.metres, now);
      case Op::Jog: motors.setSpeed(cmd.speed, now); return motors.jog(direction, now);
      case Op::Timed: motors.setSpeed(cmd.speed, now); return motors.moveFor(direction, cmd.duration, now);
      case Op::Start: return startProgram(motors.program, now);
      case Op::Program: motors.setSpeed(cmd.speed, now); return startProgram(cmd.value, now);
      default: return false;
    }
  }
  void tick(uint32_t now, bool linkOk) {
    battery.tick(now);
    const bool permit = linkOk && battery.allowed(now);
    if (!permit) servo.off();
    motors.tick(now, permit);
    if (motors.fault) servo.off();
    servo.tick(now);
    const bool operational = permit && !motors.fault;
    if (!operational || now - testAt_ >= 3000) test_ = LightTest::Off;
    const bool moving = motors.applied[0] || motors.applied[1] || servo.moving(now);
    if (moving && !moving_) blinkAt_ = now;
    moving_ = moving;
    const bool strobing = servo.enabled && servo.angle > 0 && servo.angle < 180;
    if (strobing && !strobing_) strobeAt_ = now;
    strobing_ = strobing;
    const bool testing = test_ != LightTest::Off;
    const bool testBlink = (now - testAt_) / 125 % 2 == 0;
    led(RedPin, operational && (testing ? test_ == LightTest::Red && testBlink : moving && (now - blinkAt_) / 125 % 2 == 0), red_);
    led(GreenPin, operational && (testing ? test_ == LightTest::Green && testBlink : !moving), green_);
    led(StrobePin, operational && (testing ? test_ == LightTest::Strobe && testBlink : strobing && (now - strobeAt_) / 125 % 2 == 0), strobe_);
  }
  Status status(uint32_t now) const {
    Status s;
    s.position = motors.position; s.target = motors.target;
    for (unsigned i = 0; i < 2; ++i) {
      s.count[i] = motors.count[i]; s.pwm[i] = motors.applied[i];
      s.countsPerSecond[i] = motors.countsPerSecond[i];
      if (motors.calibrated()) s.wheelMps[i] = motors.countsPerSecond[i] * MetersPerCount[i];
    }
    s.maxMps = motors.maxSpeedMps();
    s.lightTest = test_;
    s.mv = battery.valid ? battery.millivolts : 0;
    s.amps = battery.amps;
    s.currentValid = battery.currentValid;
    s.mode = motors.mode; s.speed = motors.speed; s.program = motors.program;
    s.remainingMs = motors.remainingMs(now);
    s.servo = servo.enabled; s.servoTarget = servo.angle; s.servoSelected = servo.selected;
    s.ready = DriveCommissioned && battery.ready(now) && motors.encodersReady && !motors.fault;
    s.powered = battery.allowed(now) && !motors.fault;
    s.locked = battery.locked; s.fault = motors.fault;
    s.centered = motors.centered; s.calibrated = motors.calibrated();
    return s;
  }
 private:
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
    log(pin == RedPin ? "LED_R" : pin == GreenPin ? "LED_G" : "STROBE", "%s", on ? "on" : "off");
  }
  MotorGroup motors;
  BatteryMonitor battery;
  ServoOutput servo;
  LightTest test_ = LightTest::Off;
  uint32_t testAt_ = 0, blinkAt_ = 0, strobeAt_ = 0;
  bool red_ = false, green_ = false, strobe_ = false, moving_ = false, strobing_ = false;
};
}
