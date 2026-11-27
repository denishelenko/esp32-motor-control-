#pragma once
#include "BatteryMonitor.h"

namespace bk370 {
enum class Direction : int8_t { Left = -1, Right = 1 };
enum class Mode : uint8_t { Idle, Target, Program, Jog, Timed, Survey, Park };
enum class Op : uint8_t { Stop, Speed, Target, Program, Servo, Jog, Timed, Zero, Reset, Start, Idle, Abort, MotorStop, LightTest, Survey, ClearReference, Park, PwmLimit };
enum class LightTest : uint8_t { Off, Red, Green, Strobe };
struct Command {
  Op op = Op::Idle;
  int8_t direction = 0;
  uint8_t speed = uint8_t(DefaultSpeedKmh), value = 0;
  uint32_t duration = 0;
  float metres = 0;
};

inline float nextProgramTarget(uint8_t program, float position, float low, float high, Direction direction) {
  const bool right = position <= low + 0.1f || (position < high - 0.1f && direction == Direction::Right);
  if (program == 1) return position >= high - 0.1f ? low : high;
  if (program == 2) {
    const float rel = position - low;
    const float steps = right ? floorf((rel + 0.05f) / 2) + 1 : ceilf((rel - 0.05f) / 2) - 1;
    return constrain(low + 2.0f * steps, low, high);
  }
  const float distance = program == 3 ? random(10, 31) / 10.0f : random(40, 71) / 10.0f;
  float step = random(2) ? distance : -distance;
  if (position + step < low || position + step > high) step = -step;
  return constrain(position + step, low, high);
}

class MotorGroup {
 public:
  int64_t count[2]{};
  float countsPerSecond[2]{};
  uint8_t speed = uint8_t(DefaultSpeedKmh), program = 1, maxPwm = DefaultMaxPwmPercent;
  Mode mode = Mode::Idle;
  float position = 0, target = 0;
  int16_t applied[2]{};
  bool centered = false, encodersReady = false;
  float fromReference() const { return side_ ? -side_ * (position - referenceM_) : position - referenceM_; }
  float wheelPosition(bool right) const {
    const unsigned index = right == Motor1OnRight ? 0 : 1;
    return referenceM_ + float(count[index] - origin_[index]) * metersPerCount_[index] +
        (right ? HalfWheelbaseM : -HalfWheelbaseM);
  }
  bool faulted() const { return fault != MotorFault::None; }
  MotorFault fault = MotorFault::None;
  uint8_t faultMotor = 0;

  void begin(int side, bool carriage2) {
    settings_ = motorSettingsFor(carriage2);
    side_ = side;
    if (side_) {
      low_ = side_ < 0 ? -SharedOuterM : SharedInnerM;
      high_ = side_ < 0 ? -SharedInnerM : SharedOuterM;
    }
    bool ready = true;
    encodersReady = true;
    for (unsigned i = 0; i < 2; ++i) {
      const auto &cfg = settings_[i];
      const float measuredCounts = cfg.effectiveCountsPerMeter();
      countsPerMeter_[i] = measuredCounts > 0 ? measuredCounts :
          (cfg.wheelCircumferenceMm > 0 ? NominalCountsPerWheel * 1000.0 / cfg.wheelCircumferenceMm : 0);
      metersPerCount_[i] = countsPerMeter_[i] > 0 ? 1.0f / countsPerMeter_[i] : 0;
      const double countsPerTurn = cfg.countsPerWheelTurn > 0 ? cfg.countsPerWheelTurn : NominalCountsPerWheel;
      referenceCountsPerSecond_[i] = cfg.maxCountsPerSecond > 0 ? cfg.maxCountsPerSecond : NominalWheelRpm * countsPerTurn / 60.0;
      wheelTurnMetres_[i] = countsPerTurn * metersPerCount_[i];
      log("MOTOR", "M%u reverseMotor=%d reverseEncoder=%d counts/m=%.1f", i + 1,
          cfg.reverseMotor, cfg.reverseEncoder, countsPerMeter_[i]);
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
      if (!pwmReady_[i]) latch(MotorFault::PwmOutput, i + 1);
      ready &= pwmReady_[i];
      const bool encoderReady = encoder_[i].begin(cfg);
      if (!encoderReady) latch(MotorFault::EncoderRead, i + 1);
      encodersReady &= encoderReady;
    }
    hardwareReady_ = encodersReady && ready;
    log("MOTOR", "output=%d encoders=%d", ready, encodersReady);
  }
  bool calibrated() const {
    return countsPerMeter_[0] > 0 && countsPerMeter_[1] > 0;
  }
  bool speedCalibrated() const { return maxSpeedMps() > 0; }
  float maxSpeedMps() const {
    return min(referenceCountsPerSecond_[0] * metersPerCount_[0] * pwmCeiling(0),
               referenceCountsPerSecond_[1] * metersPerCount_[1] * pwmCeiling(1));
  }
  bool setPwmLimit(uint8_t percent) {
    if (!percent || percent > 100) return false;
    if (maxPwm != percent) log("PWM", "drive limit %u -> %u%%", maxPwm, percent);
    maxPwm = percent;
    return true;
  }
  float wheelMps(unsigned i) const { return countsPerSecond[i] * metersPerCount_[i]; }
  bool ready() const { return hardwareReady_ && encodersReady && !faulted(); }
  bool surveyed() const { return surveyed_; }
  bool clearReference(uint32_t now) {
    if (!side_ || mode != Mode::Idle || now - lastEncoderChange_ < 350) return false;
    centered = surveyed_ = false;
    stopPending_ = false;
    log("ENCODER", "shared cable reference cleared");
    return true;
  }
  bool atOuterMark() const {
    return centered && mode == Mode::Idle && fabsf(position - side_ * SharedOuterM) <= TargetToleranceM;
  }
  bool parked(uint32_t now) const {
    return side_ && centered && mode == Mode::Idle && now - lastEncoderChange_ >= BrakeQuietMs &&
           fabsf(position - side_ * SharedOuterM) <= ParkToleranceM;
  }
  bool park(uint32_t now) {
    if (!side_ || !surveyed_ || mode != Mode::Idle) return false;
    if (parked(now)) return true;
    parkCorrections_ = 0;
    return beginTarget(side_ * SharedOuterM, Mode::Park, now);
  }
  bool survey(uint32_t now) {
    if (!side_ || !atOuterMark() || now - lastEncoderChange_ < BrakeQuietMs) return false;
    surveyed_ = false;
    return beginTarget(side_ * SharedInnerM, Mode::Survey, now);
  }
  bool zero(uint32_t now) {
    if (!encodersReady || mode != Mode::Idle || now - lastEncoderChange_ < 350) return false;
    if (side_ && centered && !atOuterMark()) return false;
    for (unsigned i = 0; i < 2; ++i) origin_[i] = count[i];
    centered = true;
    surveyed_ = false;
    referenceM_ = side_ * SharedOuterM;
    position = target = referenceM_;
    stopPending_ = false;
    log("ENCODER", "reference established at %.3f m", referenceM_);
    return true;
  }
  void setSpeed(uint8_t kmh) {
    if (!speedPreset(kmh) || speed == kmh) return;
    log("MOTOR", "speed limit=%u -> %u km/h", speed, kmh);
    speed = kmh;
  }
  bool moveFor(Direction direction, uint32_t ms, uint32_t now) {
    if (side_ || !ms || ms > 10000 || !validDirection(direction)) return false;
    if (!start(Mode::Timed, direction, now, ms)) return false;
    log("MOVE", "START %s timed %lu ms", directionName(), (unsigned long)ms);
    return true;
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
    if (id < 1 || id > 4 || (side_ && !surveyed_)) return false;
    program = id;
    const float first = id == 1 && position < high_ - 0.1f ? high_ : nextTarget();
    return beginTarget(first, Mode::Program, now);
  }
  bool jog(Direction direction, uint32_t now) {
    if (!validDirection(direction)) return false;
    if (side_ && !surveyed_ && (centered || int8_t(direction) != side_)) return false;
    if (mode != Mode::Jog || direction_ != direction) {
      if (!start(Mode::Jog, direction, now, 650)) return false;
      target = direction == Direction::Right ? high_ : low_;
      log("MOVE", "START %s jog from=%+.3f m", directionName(), position);
    }
    jogAt_ = now;
    return true;
  }
  uint32_t remainingMs(uint32_t now) const {
    return mode == Mode::Timed && now - started_ < duration_ ? duration_ - (now - started_) : 0;
  }
  void stop(uint32_t now, const char *reason) {
    if (mode != Mode::Idle && !waiting_) logMoveEnd(now, reason);
    if (positionCommand() && !waiting_) {
      stoppedTarget_ = target;
      stoppedOrigin_ = segmentStartM_;
      stoppedDirection_ = int8_t(direction_);
      stopAt_ = now;
      stopPending_ = true;
    }
    if (applied[0] || applied[1]) lastPowered_ = now;
    mode = Mode::Idle;
    waiting_ = false;
    finalPhase_ = FinalPhase::Drive;
    for (unsigned i = 0; i < 2; ++i) { integral_[i] = 0; write(i, 0, now); }
  }
  void tick(uint32_t now, bool permit) {
    permitted_ = permit;
    for (unsigned i = 0; i < 2; ++i) {
      int64_t current;
      if (!encoder_[i].read(current)) {
        encodersReady = false;
        latch(MotorFault::EncoderRead, i + 1);
        stop(now, "encoder read error");
        return;
      }
      if (count[i] != current) lastEncoderChange_ = now;
      count[i] = current;
    }
    const uint32_t sampleMs = now - sampledAt_;
    const bool freshSpeed = sampleMs >= 50;
    if (freshSpeed) {
      for (unsigned i = 0; i < 2; ++i) {
        countsPerSecond[i] = float(count[i] - sampledCounts_[i]) * 1000.0f / sampleMs;
        sampledCounts_[i] = count[i];
      }
      sampledAt_ = now;
      speedSampleSeconds_ = min<uint32_t>(sampleMs, 100) / 1000.0f;
    }
    float wheel[2]{};
    if (centered && calibrated()) {
      for (unsigned i = 0; i < 2; ++i)
        wheel[i] = referenceM_ + float(count[i] - origin_[i]) * metersPerCount_[i];
      position = (wheel[0] + wheel[1]) * 0.5f;
      if (stopPending_ && now - stopAt_ >= BrakeQuietMs && now - lastEncoderChange_ >= BrakeQuietMs) {
        saveStop(stoppedTarget_, stoppedOrigin_, stoppedDirection_);
        stopPending_ = false;
      }
    }
    if (side_ && centered && !faulted() &&
        (min(wheel[0], wheel[1]) < low_ - SharedOverrunM ||
         max(wheel[0], wheel[1]) > high_ + SharedOverrunM)) {
      surveyed_ = false;
      latch(MotorFault::TrackOverrun);
      stop(now, "shared track boundary overrun");
      return;
    }
    if (!permit || faulted()) { stop(now, "interlock"); return; }
    if (mode == Mode::Idle) return;
    const uint32_t elapsed = now - started_;
    if ((mode == Mode::Timed && elapsed >= duration_) || (mode == Mode::Jog && now - jogAt_ >= 500U) ||
        (mode != Mode::Timed && elapsed >= ((mode == Mode::Survey || mode == Mode::Park) ? 65000U : 30000U) && !waiting_)) {
      stop(now, "time limit");
      return;
    }
    if (centered && calibrated() && fabsf(wheel[0] - wheel[1]) > 0.5f) {
      latch(MotorFault::WheelMismatch);
      stop(now, "encoders disagree");
      return;
    }
    const int sign = int8_t(direction_);
    const float boundaryLeft = sign > 0 ? high_ - max(wheel[0], wheel[1]) : min(wheel[0], wheel[1]) - low_;
    if (centered && calibrated() && boundaryLeft <= FinalBrakeM &&
        (mode == Mode::Timed || mode == Mode::Jog)) { stop(now, "track limit"); return; }

    const float remaining = sign * (target - position);
    const bool positioning = positionCommand();
    if (positioning) {
      if (waiting_) {
        if (now - waitAt_ >= (program == 2 ? 350U : program == 3 ? 150U : 500U)) {
          if (!beginTarget(nextTarget(), Mode::Program, now)) stop(now, "program ended");
        }
        return;
      }

      if (serviceFinalApproach(remaining, now)) return;
      if (side_ && sign == -side_ && boundaryLeft <= 0) {
        stop(now, "inner shared track limit");
        return;
      }
    }
    const bool creeping = finalPhase_ == FinalPhase::Creep;
    for (unsigned i = 0; i < 2; ++i) {
      int64_t progress;
      if (feedback_[i].sample(count[i], now, progress) && progress <= 0) {
        latch(progress < 0 ? MotorFault::Reversed : MotorFault::NoFeedback, i + 1);
        log("MOTOR", "M%u dir=%d ticks=%lld PWM=%d", i + 1, sign, (long long)progress, applied[i]);
        stop(now, "motor feedback fault");
        return;
      }
    }
    if (now - lastPowered_ < 100) return;
    if (now - lastUpdate_ < 10) return;
    const float updateSeconds = min<uint32_t>(now - lastUpdate_, 50) / 1000.0f;
    lastUpdate_ = now;
    const float rampTime = min(1.0f, (elapsed + 10) / float(RampUpMs));
    float scale = smoothRamp(rampTime);
    if (mode == Mode::Timed && RampDownMs) scale = min(scale, (duration_ - elapsed) / float(RampDownMs));
    const float rampM = segmentLength_ <= 2.0f ? segmentLength_ / 3.0f : SlowZoneM;
    const bool approaching = (positioning && remaining <= rampM) ||
        (side_ && centered && boundaryLeft <= SlowZoneM);
    if (positioning) {
      const float traveled = max(0.0f, segmentLength_ - remaining);
      const float rampUp = max(0.08f, smoothRamp(traveled / rampM));
      const float rampDown = max(ApproachMinScale, smoothRamp(remaining / rampM));
      scale = min(scale, min(rampUp, rampDown));
    }
    const bool slowMode = mode == Mode::Survey || mode == Mode::Park || (side_ && mode == Mode::Jog);
    float limitMps = min(slowMode ? SharedSurveyMps : speed / 3.6f, maxSpeedMps());
    const bool nearBoundary = side_ && centered && boundaryLeft <= SlowZoneM;
    const float boundarySpeed = max(FinalCreepMps, sqrtf(2 * SharedBoundaryDecelMps2 * max(0.0f, boundaryLeft)));
    if (side_ && centered) limitMps = min(limitMps, boundarySpeed);
    const float metresPerTurn = min(wheelTurnMetres_[0], wheelTurnMetres_[1]);
    const float accelerationSpeed = (ApproachTurnsPerSecond + AccelTurnsPerSecondSquared * elapsed / 1000.0f) * metresPerTurn;
    float targetMps = min(scale * limitMps, accelerationSpeed);
    if (positioning) {
      const float decelTurns = max(0.0f, remaining / metresPerTurn - ApproachTurns);
      const float decelSpeed = sqrtf(ApproachTurnsPerSecond * ApproachTurnsPerSecond +
                                    2 * DecelTurnsPerSecondSquared * decelTurns) * metresPerTurn;
      targetMps = max(min(targetMps, decelSpeed), min(limitMps, FinalCreepMps));
    }
    if (creeping) targetMps = min(limitMps, FinalCreepMps);
    const float controlSeconds = speedSampleSeconds_;
    speedSampleSeconds_ = 0;
    float progressDifference = 0;
    if (calibrated()) {
      const float first = float(count[0] - segmentOrigin_[0]) * metersPerCount_[0];
      const float second = float(count[1] - segmentOrigin_[1]) * metersPerCount_[1];
      progressDifference = sign * (first - second);
    }
    if (nearBoundary && !creeping &&
        max(sign * wheelMps(0), sign * wheelMps(1)) > boundarySpeed + ApproachBrakeMarginMps) {
      for (unsigned i = 0; i < 2; ++i) { integral_[i] = 0; write(i, 0, now); }
      return;
    }
    for (unsigned i = 0; i < 2; ++i) {
      // Reduce output on the leading wheel without increasing the other wheel's duty.
      const float lead = i == 0 ? progressDifference : -progressDifference;
      const float correction = constrain(lead * 2.0f, 0.0f, 0.18f);
      const float pwmLimit = pwmCeiling(i);
      const float approachLimit = min(pwmLimit, CalibratedApproachPwmPercent[i] / 100.0f);
      float ceiling = pwmLimit;
      if (approaching) {
        const float turnsRemaining = max(0.0f, remaining / wheelTurnMetres_[i] - ApproachTurns);
        ceiling = approachLimit + (pwmLimit - approachLimit) * min(1.0f, turnsRemaining);
      }
      if (creeping) ceiling = approachLimit;
      const float feedForward = targetMps * countsPerMeter_[i] / referenceCountsPerSecond_[i];
      const float error = (targetMps - sign * wheelMps(i)) / wheelTurnMetres_[i];
      const float baseDuty = feedForward + SpeedKp * error;
      const float integralLimit = max(0.0f, ceiling - feedForward);
      integral_[i] = constrain(integral_[i], 0.0f, integralLimit);
      const float next = baseDuty + integral_[i];
      if ((error > 0 && next < ceiling && correction == 0) || (error < 0 && next > 0))
        integral_[i] = constrain(integral_[i] + SpeedKi * error * controlSeconds, 0.0f, integralLimit);
      const float fade = 1.0f - min(elapsed, StartBoostMs) / float(StartBoostMs);
      const float boost = CalibratedStartPwmPercent[i] / 100.0f * fade *
                         constrain(error / ApproachTurnsPerSecond, 0.0f, 1.0f);
      const float duty = constrain(max(baseDuty + integral_[i], boost), 0.0f, ceiling) * (1.0f - correction);
      const float slewCeiling = fabsf(applied[i]) / 1023.0f + DutyRisePerSec * updateSeconds;
      write(i, sign * int(min(duty, slewCeiling) * 1023.0f), now);
      if (faulted()) { stop(now, "motor PWM error"); return; }
    }
    if (now - telemetryAt_ >= 500) {
      telemetryAt_ = now;
      log("MOTOR", "PWM=%d/%d enc=%lld/%lld", applied[0], applied[1], (long long)count[0], (long long)count[1]);
    }
  }
 private:
  float pwmCeiling(unsigned i) const {
    return min(constrain(settings_[i].dutyMultiplier, 0.0f, 1.0f), drivePwmFraction(maxPwm));
  }
  static float smoothRamp(float fraction) {
    const float t = constrain(fraction, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }
  enum class FinalPhase : uint8_t { Drive, Brake, Creep };
  bool serviceFinalApproach(float remaining, uint32_t now) {
    if (finalPhase_ == FinalPhase::Drive && remaining > FinalBrakeM) return false;
    if (finalPhase_ == FinalPhase::Drive) finalStartedAt_ = now;
    if (now - finalStartedAt_ >= FinalApproachTimeoutMs) {
      stop(now, "braking or creep timed out");
      return true;
    }
    const float tolerance = mode == Mode::Park ? ParkToleranceM : TargetToleranceM;
    if (finalPhase_ == FinalPhase::Drive || (finalPhase_ == FinalPhase::Creep && remaining <= tolerance)) {
      finalPhase_ = FinalPhase::Brake;
      lastPowered_ = brakeAt_ = now;
      for (unsigned i = 0; i < 2; ++i) { integral_[i] = 0; write(i, 0, now); }
      log("BRAKE", "LOW/LOW remaining=%.3f m", remaining);
      if (faulted()) stop(now, "motor PWM error");
      return true;
    }
    if (finalPhase_ == FinalPhase::Creep) return false;
    if (now - brakeAt_ < BrakeQuietMs || now - lastEncoderChange_ < BrakeQuietMs) return true;
    if (mode == Mode::Park && fabsf(remaining) > tolerance) {
      if (parkCorrections_ >= ParkCorrectionLimit) {
        stop(now, "park correction limit");
        return true;
      }
      ++parkCorrections_;
      direction_ = target > position ? Direction::Right : Direction::Left;
      for (unsigned i = 0; i < 2; ++i) segmentOrigin_[i] = count[i];
      finalPhase_ = FinalPhase::Creep;
      brakeAt_ = now;
      log("PARK", "correction=%u remaining=%+.3f m", parkCorrections_, target - position);
      return true;
    }
    if (remaining > tolerance) {
      finalPhase_ = FinalPhase::Creep;
      brakeAt_ = now;
      log("BRAKE", "wheels stopped short; creep remaining=%.3f m", remaining);
      return true;
    }
    if (mode == Mode::Survey && fabsf(remaining) <= TargetToleranceM) {
      surveyed_ = true;
      log("ENCODER", "shared cable survey complete at %.3f m", position);
    }
    if (remaining < -ProgramOvershootToleranceM || mode != Mode::Program) {
      stop(now, remaining < -TargetToleranceM ? "target overshot" : "at target, wheels stopped");
      return true;
    }
    saveStop(target, segmentStartM_, int8_t(direction_));
    logMoveEnd(now, "segment complete");
    waiting_ = true;
    waitAt_ = now;
    return true;
  }
  const char *directionName() const { return direction_ == Direction::Right ? "RIGHT" : "LEFT"; }
  void logMoveEnd(uint32_t now, const char *reason) {
    log("MOVE", "END at=%+.3f m %lu ms (%s)", position, (unsigned long)(now - started_), reason);
  }
  bool positionCommand() const { return mode == Mode::Target || mode == Mode::Program || mode == Mode::Survey || mode == Mode::Park; }
  void saveStop(float commanded, float origin, int8_t direction) {
    log("POSITION", "target=%.3f m measured=%.3f m error=%+.1f cm travel=%+.3f m",
        commanded, position, direction * (position - commanded) * 100.0f, position - origin);
  }
  bool validDirection(Direction d) const { return d == Direction::Left || d == Direction::Right; }
  void latch(MotorFault reason, uint8_t motor = 0) {
    if (faulted()) return;
    fault = reason;
    faultMotor = motor;
    log("FAULT", "motor=%u %s", motor, motorFaultMessage(reason));
  }
  bool start(Mode modeWanted, Direction direction, uint32_t now, uint32_t duration = 0) {
    if (!ready() || !permitted_ || !speed ||
        !speedCalibrated()) return false;
    if (mode != Mode::Idle && !waiting_) stop(now, "replaced");
    stopPending_ = false;
    mode = modeWanted;
    direction_ = direction;
    started_ = now;
    duration_ = duration;
    waiting_ = false;
    finalPhase_ = FinalPhase::Drive;
    for (unsigned i = 0; i < 2; ++i) {
      segmentOrigin_[i] = count[i];
      integral_[i] = 0;
    }
    return true;
  }
  bool beginTarget(float metres, Mode m, uint32_t now) {
    if (!centered || !calibrated() || (side_ && !surveyed_ && m != Mode::Survey) ||
        !isfinite(metres) || metres < low_ || metres > high_) return false;
    if (fabsf(metres - position) <= (m == Mode::Park ? ParkToleranceM : TargetToleranceM)) {
      stop(now, "already at target");
      return true;
    }
    if (!start(m, metres > position ? Direction::Right : Direction::Left, now)) return false;
    segmentStartM_ = position;
    segmentLength_ = fabsf(metres - position);
    target = metres;
    log("MOVE", "START %s from=%+.3f m target=%+.3f m distance=%.3f m",
        directionName(), segmentStartM_, target, segmentLength_);
    return true;
  }
  float nextTarget() const { return nextProgramTarget(program, position, low_, high_, direction_); }
  void write(unsigned i, int val, uint32_t now) {
    if (!pwmReady_[i] || val == applied[i]) return;
    const auto &cfg = settings_[i];
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
      latch(MotorFault::PwmOutput, i + 1);
      feedback_[i].update(0, count[i], now);
      applied[i] = 0;
      return;
    }
    if ((!val) != (!applied[i])) log("PWM", "motor=%u duty=%d/1023 time=%lu", i + 1, val, (unsigned long)now);
    feedback_[i].update(val, count[i], now);
    applied[i] = val;
  }
  uint8_t parkCorrections_ = 0;
  bool stopPending_ = false;
  uint32_t stopAt_ = 0;
  float stoppedTarget_ = 0, stoppedOrigin_ = 0;
  int8_t stoppedDirection_ = 0;
  int side_ = 0;
  bool surveyed_ = false;
  float low_ = -LimitM, high_ = LimitM, referenceM_ = 0;
  float segmentStartM_ = 0, segmentLength_ = 0, speedSampleSeconds_ = 0;
  const MotorSettings *settings_ = Motors;
  float countsPerMeter_[2]{}, metersPerCount_[2]{}, referenceCountsPerSecond_[2]{}, wheelTurnMetres_[2]{};
  Encoder encoder_[2];
  MotorFeedbackCheck feedback_[2];
  int64_t origin_[2]{}, segmentOrigin_[2]{};
  int64_t sampledCounts_[2]{};
  float integral_[2]{};
  bool hardwareReady_ = false, permitted_ = false, waiting_ = false, pwmReady_[2]{};
  Direction direction_ = Direction::Right;
  FinalPhase finalPhase_ = FinalPhase::Drive;
  uint32_t started_ = 0, duration_ = 0, lastEncoderChange_ = 0, jogAt_ = 0;
  uint32_t lastPowered_ = 0, lastUpdate_ = 0, waitAt_ = 0, telemetryAt_ = 0;
  uint32_t sampledAt_ = 0;
  uint32_t finalStartedAt_ = 0, brakeAt_ = 0;
};

struct Status {
#ifdef BK_MIXED_GAN
  float estimatedTravelLimit = 0;
#endif
  float position = 0, target = 0, fromReference = 0, wheelPosition[2]{};
  int32_t count[2]{};
  int16_t pwm[2]{};
  float countsPerSecond[2]{}, wheelMps[2]{}, maxMps = 0;
  uint16_t mv = 0;
  float amps = 0;
  InaPeaks inaPeaks;
  bool currentValid = false, hasIna226 = false;
  uint32_t remainingMs = 0;
  Mode mode = Mode::Idle;
  LightTest lightTest = LightTest::Off;
  uint8_t speed = uint8_t(DefaultSpeedKmh), program = 1, servoTarget = 0, servoSelected = 45;
  MotorFault fault = MotorFault::None;
  uint8_t faultMotor = 0, maxPwm = DefaultMaxPwmPercent;
  bool ready = false, powered = false, locked = false, centered = false, calibrated = false, servo = false, surveyed = false, atOuterMark = false, parked = false;
};

template<bool UseIna226>
class Carriage {
 public:
  void begin(int side, bool carriage2) {
    motors.begin(side, carriage2);
    if constexpr (UseIna226) battery.begin();
    servo.begin();
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
    if (motors.mode == Mode::Park && cmd.op != Op::Park) motors.stop(now, "park interrupted");
    if (cmd.op == Op::Abort) { stop(now, "safety stop"); return true; }
    if (cmd.op == Op::Stop || cmd.op == Op::MotorStop) {
      motors.stop(now, "command · brake LOW/LOW");
      test_ = LightTest::Off;
      if (cmd.op == Op::Stop) {
        if (powerAllowed(now)) servo.close(now);
        else servo.off();
      }
      return true;
    }
    if (cmd.op == Op::Zero) return motors.zero(now);
    if (cmd.op == Op::ClearReference) return motors.clearReference(now);
    if (cmd.op == Op::Reset) {
      if constexpr (UseIna226) return battery.reset(now);
      return false;
    }
    if (cmd.op == Op::PwmLimit) return motors.setPwmLimit(cmd.value);
    if (cmd.op == Op::Speed) { motors.setSpeed(cmd.speed); return true; }
    if (cmd.op == Op::Servo && cmd.value == 0) {
      if (powerAllowed(now)) servo.close(now);
      else servo.off();
      return true;
    }
    if (!powerReady(now) || motors.faulted()) return false;
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
      case Op::Target: motors.setSpeed(cmd.speed); return motors.moveTo(cmd.metres, now);
      case Op::Jog: motors.setSpeed(cmd.speed); return motors.jog(direction, now);
      case Op::Timed: motors.setSpeed(cmd.speed); return motors.moveFor(direction, cmd.duration, now);
      case Op::Survey: return motors.survey(now);
      case Op::Park:
        if (!motors.park(now)) return false;
        servo.close(now);
        return true;
      case Op::Start: motors.setSpeed(cmd.speed); return startProgram(motors.program, now);
      case Op::Program: motors.setSpeed(cmd.speed); return startProgram(cmd.value, now);
      default: return false;
    }
  }
  void tick(uint32_t now, bool linkOk) {
    if constexpr (UseIna226) battery.tick(now);
    const bool permit = linkOk && powerAllowed(now);
    if (!permit) servo.off();
    motors.tick(now, permit);
    if constexpr (UseIna226) battery.motorPwm(now, motors.applied[0] || motors.applied[1]);
    if (motors.faulted()) servo.off();
    servo.tick(now);
    const bool operational = permit && !motors.faulted();
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
    s.fromReference = motors.fromReference();
    s.wheelPosition[0] = motors.wheelPosition(false);
    s.wheelPosition[1] = motors.wheelPosition(true);
    for (unsigned i = 0; i < 2; ++i) {
      s.count[i] = motors.count[i]; s.pwm[i] = motors.applied[i];
      s.countsPerSecond[i] = motors.countsPerSecond[i];
      s.wheelMps[i] = motors.wheelMps(i);
    }
    s.maxMps = motors.maxSpeedMps();
    s.maxPwm = motors.maxPwm;
    s.lightTest = test_;
    s.hasIna226 = UseIna226;
    if constexpr (UseIna226) {
      s.mv = battery.valid ? battery.millivolts : 0;
      s.amps = battery.amps;
      s.inaPeaks = battery.peaks;
      s.currentValid = battery.currentValid;
      s.locked = battery.locked;
    }
    s.mode = motors.mode; s.speed = motors.speed; s.program = motors.program;
    s.remainingMs = motors.remainingMs(now);
    s.servo = servo.enabled; s.servoTarget = servo.angle; s.servoSelected = servo.selected;
    s.ready = powerReady(now) && motors.ready();
    s.powered = powerAllowed(now) && !motors.faulted();
    s.fault = motors.fault;
    s.faultMotor = motors.faultMotor;
    s.surveyed = motors.surveyed(); s.atOuterMark = motors.atOuterMark(); s.parked = motors.parked(now);
    s.centered = motors.centered; s.calibrated = motors.calibrated();
    return s;
  }
 private:
  bool powerAllowed(uint32_t now) const {
    if constexpr (UseIna226) return battery.allowed(now);
    return true;
  }
  bool powerReady(uint32_t now) const {
    if constexpr (UseIna226) return battery.ready(now);
    return true;
  }
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
  }
  MotorGroup motors;
  BatteryMonitor battery;
  ServoOutput servo;
  LightTest test_ = LightTest::Off;
  uint32_t testAt_ = 0, blinkAt_ = 0, strobeAt_ = 0;
  bool red_ = false, green_ = false, strobe_ = false, moving_ = false, strobing_ = false;
};
}
