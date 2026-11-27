#pragma once

#include <Motion.h>
#include "PairSettings.h"
#include "../../12GAN20-20/Arduino IDE/2_Carriages_WebSite/ESP32S3_Carriage_2/config.h"

namespace bk370 {
class GanCarriage {
 public:
  void begin(int, bool) {
    static_assert(ServoPin == SERVO_SIGNAL_PIN);
    for (uint8_t pin : {MOTOR_PWM_PIN, MOTOR_IN1_PIN, MOTOR_IN2_PIN, LIGHT_RED_PIN, LIGHT_GREEN_PIN, MOSFET_PIN}) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
    ready_ = ledcAttachChannel(MOTOR_PWM_PIN, PWM_FREQUENCY_HZ, 10, 0) && ledcWrite(MOTOR_PWM_PIN, 0);
    servo_.begin();
    leftMps_ = sampleSpeed(pair::GanLeft);
    rightMps_ = sampleSpeed(pair::GanRight);
    log("GAN", "ready=%d cable=%.2f m calibration left=%.3f right=%.3f m/s",
        ready_, pair::GanCableLengthM, leftMps_, rightMps_);
  }

  void stop(uint32_t now, const char *reason) {
    brake(now, reason);
    servo_.off();
    lightTest_ = LightTest::Off;
  }

  bool execute(const Command &cmd, uint32_t now) {
    if (cmd.op == Op::Abort) { stop(now, "abort"); return true; }
    if (cmd.op == Op::Stop || cmd.op == Op::MotorStop) {
      brake(now, "command");
      if (cmd.op == Op::Stop) servo_.close(now);
      return true;
    }
    if (!ready_ || !permit_) return false;
    if (cmd.op == Op::Speed) return true;  // GAN uses its measured, fixed PWM.
    if (cmd.op == Op::Servo) {
      if (!cmd.value) { servo_.close(now); return true; }
      return servo_.cycle(cmd.value, now);
    }
    if (cmd.op == Op::LightTest) {
      if (mode_ != Mode::Idle || cmd.value < 1 || cmd.value > uint8_t(LightTest::Strobe)) return false;
      lightTest_ = static_cast<LightTest>(cmd.value);
      lightAt_ = now;
      return true;
    }
    if (cmd.op == Op::Zero) {
      if (mode_ != Mode::Idle || now - stoppedAt_ < 500 || !calibrated()) return false;
      position_ = target_ = uncertainty_ = 0;
      centered_ = true;
      log("GAN", "physical centre confirmed by operator");
      return true;
    }
    if (!centered_ || !calibrated() || limit() <= TargetToleranceM) return false;
    if (cmd.op == Op::Target) return beginTarget(cmd.metres, Mode::Target, now);
    if (cmd.op == Op::Start || cmd.op == Op::Program) {
      if (cmd.op == Op::Program && (cmd.value < 1 || cmd.value > 4)) return false;
      if (cmd.op == Op::Program) program_ = cmd.value;
      const float edge = limit();
      const float target = program_ == 1 && position_ < edge - 0.1f ? edge :
          nextProgramTarget(program_, position_, -edge, edge, direction_);
      if (!beginTarget(target, Mode::Program, now)) return false;
      if (servo_.cycle(servo_.selected, now)) return true;
      brake(now, "servo output error");
      return false;
    }
    if (cmd.op == Op::Jog && (cmd.direction == -1 || cmd.direction == 1)) {
      const auto direction = static_cast<Direction>(cmd.direction);
      if (mode_ != Mode::Jog || direction_ != direction) {
        const float edge = limit();
        if (cmd.direction * position_ >= edge || !beginTarget(cmd.direction * edge, Mode::Jog, now)) return false;
      }
      jogAt_ = now;
      return true;
    }
    return false;
  }

  void tick(uint32_t now, bool linkOk) {
    integrate(now);
    if (!linkOk && permit_) stop(now, "ESP-NOW lost");
    permit_ = linkOk;
    if (permit_ && ready_) {
      serviceMotion(now);
      if (ready_) servo_.tick(now);
      else servo_.off();
    }
    if (now - lightAt_ >= 3000) lightTest_ = LightTest::Off;
    const bool moving = applied_ || servo_.moving(now);
    const bool blink = (now / LIGHT_BLINK_HALF_PERIOD_MS) % 2 == 0;
    const bool test = lightTest_ != LightTest::Off;
    digitalWrite(LIGHT_RED_PIN, permit_ && blink && (test ? lightTest_ == LightTest::Red : moving));
    digitalWrite(LIGHT_GREEN_PIN, permit_ && (test ? lightTest_ == LightTest::Green && blink : !moving));
    digitalWrite(MOSFET_PIN, permit_ && blink && (test ? lightTest_ == LightTest::Strobe : servo_.enabled && servo_.angle > 0));
  }

  Status status(uint32_t) const {
    Status s;
    s.position = s.fromReference = position_;
    s.target = target_;
    s.estimatedTravelLimit = limit();
    s.ready = s.powered = ready_;
    s.calibrated = calibrated();
    s.centered = centered_;
    s.mode = mode_;
    s.program = program_;
    s.pwm[0] = applied_;
    s.maxMps = s.calibrated ? max(leftMps_, rightMps_) : 0;
    s.speed = pair::GanPwmPercent;
    s.servo = servo_.enabled;
    s.servoTarget = servo_.angle;
    s.servoSelected = servo_.selected;
    s.lightTest = lightTest_;
    if (!ready_) s.fault = MotorFault::PwmOutput;
    return s;
  }

 private:
  static constexpr int RunDuty = PWM_MAX * pair::GanPwmPercent / 100;
  // Match the GAN calibration sketch: +24/1023 every 10 ms.
  static constexpr int DutyStep = 24;
  static constexpr uint32_t MotorPeriodMs = 10;
  static float sampleSpeed(pair::TravelSample sample) {
    const float rampSeconds = (RunDuty + DutyStep - 1) / DutyStep * MotorPeriodMs / 1000.0f;
    if (sample.timeMs < 1000 || sample.timeMs > 10500 || !isfinite(sample.distanceM) || sample.distanceM <= 0)
      return 0;
    return sample.distanceM / (sample.timeMs / 1000.0f - rampSeconds / 2);
  }

  bool calibrated() const {
    return leftMps_ > 0 && rightMps_ > 0 && pair::GanCableLengthM / 2 > pair::GanEdgeReserveM;
  }
  float limit() const {
    return max(0.0f, min(LimitM, pair::GanCableLengthM / 2 - pair::GanEdgeReserveM - uncertainty_));
  }
  void integrate(uint32_t now) {
    if (applied_ && centered_) {
      const float speed = applied_ > 0 ? rightMps_ : leftMps_;
      const float delta = speed * applied_ / RunDuty * ((now - integratedAt_) / 1000.0f);
      position_ += delta;
      uncertainty_ += fabsf(delta) * pair::GanDriftFraction;
    }
    integratedAt_ = now;
  }
  void write(int duty) {
    if (!ready_ || duty == applied_) return;
    const int physical = MOTOR_DIRECTION_REVERSED ? -duty : duty;
    if (!duty) {
      if (ledcWrite(MOTOR_PWM_PIN, 0)) {
        digitalWrite(MOTOR_IN1_PIN, LOW);
        digitalWrite(MOTOR_IN2_PIN, LOW);
        applied_ = 0;
        return;
      }
    } else {
      digitalWrite(MOTOR_IN1_PIN, physical > 0);
      digitalWrite(MOTOR_IN2_PIN, physical < 0);
      if (ledcWrite(MOTOR_PWM_PIN, abs(physical))) { applied_ = duty; return; }
    }
    ledcDetach(MOTOR_PWM_PIN);
    pinMode(MOTOR_PWM_PIN, OUTPUT);
    digitalWrite(MOTOR_PWM_PIN, LOW);
    digitalWrite(MOTOR_IN1_PIN, LOW);
    digitalWrite(MOTOR_IN2_PIN, LOW);
    ready_ = false;
    applied_ = 0;
    mode_ = Mode::Idle;
  }
  void brake(uint32_t now, const char *reason) {
    integrate(now);
    if (applied_) uncertainty_ += pair::GanStopReserveM;
    if (mode_ != Mode::Idle && !waiting_)
      log("MOVE", "END estimated=%+.2f m (%s)", position_, reason);
    if (mode_ != Mode::Idle) stoppedAt_ = now;
    write(0);
    mode_ = Mode::Idle;
    waiting_ = false;
    target_ = position_;
  }
  bool beginTarget(float target, Mode mode, uint32_t now) {
    integrate(now);
    if (!isfinite(target) || fabsf(target) > limit() + 0.0001f) return false;
    if (mode_ != Mode::Idle) brake(now, "new segment");
    if (!ready_) return false;
    target_ = constrain(target, -limit(), limit());
    if (fabsf(target_ - position_) <= TargetToleranceM) return true;
    direction_ = target_ > position_ ? Direction::Right : Direction::Left;
    log("MOVE", "START %s estimate from=%+.2f m target=%+.2f m",
        direction_ == Direction::Right ? "RIGHT" : "LEFT", position_, target_);
    mode_ = mode;
    startedAt_ = stoppedAt_ = now;
    lightTest_ = LightTest::Off;
    return true;
  }
  void serviceMotion(uint32_t now) {
    if (mode_ == Mode::Idle) return;
    if ((mode_ == Mode::Jog && now - jogAt_ >= 500) || now - startedAt_ >= 60000 ||
        limit() <= TargetToleranceM) {
      brake(now, "time or travel limit");
      return;
    }
    if (waiting_) {
      if (now - stoppedAt_ < 500) return;
      const float edge = limit();
      const float next = nextProgramTarget(program_, position_, -edge, edge, direction_);
      if (!beginTarget(next, Mode::Program, now)) brake(now, "program boundary");
      return;
    }
    const int direction = int8_t(direction_);
    const float target = constrain(target_, -limit(), limit());
    if (direction * (target - position_) <= TargetToleranceM) {
      const bool repeat = mode_ == Mode::Program;
      brake(now, "estimated target");
      if (repeat && ready_) {
        mode_ = Mode::Program;
        waiting_ = true;
      }
      return;
    }
    if (now - stoppedAt_ < 100 || now - rampAt_ < MotorPeriodMs) return;
    rampAt_ = now;
    write(direction * min(RunDuty, abs(applied_) + DutyStep));
  }

  ServoOutput servo_;
  Direction direction_ = Direction::Right;
  Mode mode_ = Mode::Idle;
  LightTest lightTest_ = LightTest::Off;
  uint8_t program_ = 1;
  bool ready_ = false, permit_ = false, centered_ = false, waiting_ = false;
  int applied_ = 0;
  float leftMps_ = 0, rightMps_ = 0, position_ = 0, target_ = 0, uncertainty_ = 0;
  uint32_t integratedAt_ = 0, startedAt_ = 0, stoppedAt_ = 0, jogAt_ = 0, rampAt_ = 0, lightAt_ = 0;
};
}
