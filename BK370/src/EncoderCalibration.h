#pragma once
#include <stdint.h>
#include <math.h>

// Encoder distance is relative to a separately established origin and does not
// correct for wheel slip. Values must be measured for each motor independently.
struct EncoderCalibration {
  float countsPerMeter = 0;
  bool distanceMeters(int64_t count, float &meters) const {
    if (!isfinite(countsPerMeter) || countsPerMeter <= 0) return false;
    meters = static_cast<float>(count) / countsPerMeter;
    return isfinite(meters);
  }
  bool calibrate(int64_t countDelta, float measuredMeters) {
    if (!isfinite(measuredMeters) || measuredMeters <= 0 || countDelta == 0) return false;
    float counts = fabsf(static_cast<float>(countDelta)) / measuredMeters;
    if (!isfinite(counts) || counts <= 0) return false;
    countsPerMeter = counts;
    return true;
  }
};
