#pragma once
#include <stdint.h>

namespace bk370::pair {
constexpr uint8_t BkMac[6] = {};
constexpr uint8_t GanMac[6] = {};
constexpr char WifiSsid[] = "";
constexpr char WifiPassword[] = "";

// Enter both actual tests at GanPwmPercent, including the distance after stopping.
struct TravelSample { uint32_t timeMs; float distanceM; };
constexpr uint8_t GanPwmPercent = 50;
constexpr TravelSample GanLeft{0, 0};
constexpr TravelSample GanRight{0, 0};
constexpr float GanCableLengthM = 0;
constexpr float GanEdgeReserveM = 1.0f;
constexpr float GanDriftFraction = 0.08f;
constexpr float GanStopReserveM = 0.15f;
static_assert(GanPwmPercent > 0 && GanPwmPercent <= 100);
static_assert(GanEdgeReserveM > 0 && GanDriftFraction > 0 && GanStopReserveM > 0);
}
