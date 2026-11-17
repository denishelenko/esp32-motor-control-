#pragma once
// Nonblocking command facade. Positions/distances are estimates, not sensors.
enum class Direction : int8_t { Left = -1, Right = 1 };
class MotorGroup {
  uint8_t index_;
public:
  explicit MotorGroup(uint8_t index) : index_(index) {}
  bool setSpeed(uint8_t percent) {
    if (index_ >= 2 || percent < 1 || percent > 100) return false;
    auto &c = carriages[index_];
    if (c.speed != percent) {
      eventLog("COMMAND", "SPEED", "carriage=%u %u->%u%%", index_+1, c.speed, percent);
      c.speed = percent;
    }
    return true;
  }
  bool moveFor(Direction direction, uint32_t durationMs) {
    if (index_ >= 2 || !durationMs || (direction != Direction::Left && direction != Direction::Right)) return false;
    startTimedTest(index_, int8_t(direction), carriages[index_].speed, durationMs);
    return true;
  }
  bool moveDistanceM(Direction direction, float metres) {
    if (index_ >= 2 || !isfinite(metres) || metres <= 0 || (direction != Direction::Left && direction != Direction::Right)) return false;
    const float target = carriages[index_].position + int8_t(direction)*metres;
    if (target < -TRACK_LIMIT_M || target > TRACK_LIMIT_M) return false;
    setCarriageTarget(index_, target, carriages[index_].speed);
    return true;
  }
  bool moveDistanceCm(Direction direction, float cm) { return moveDistanceM(direction, cm / 100.0f); }
  void stop() { if (index_ < 2) stopCarriage(index_, true); }
};
