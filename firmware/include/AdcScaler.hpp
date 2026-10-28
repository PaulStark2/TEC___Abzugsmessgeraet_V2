#pragma once

#include <cstdint>

// Provisional 12-bit map until ForceCalibration is active on the STM32 (Phase 7):
// 0 counts = -range_N, 4095 counts = +range_N. Default range is MachineConfig ±30 N.
// The argument is a float so a window mean is not rounded before the scale.
namespace adc_scaler
{

constexpr std::uint16_t kAdcFullScale = 4095;
constexpr float kDefaultRangeN = 30.0f;
constexpr float kVref = 3.3f;

inline float countsToNewton(float counts, float range_N = kDefaultRangeN)
{
    if (range_N <= 0.0f)
    {
        range_N = kDefaultRangeN;
    }
    if (counts < 0.0f)
    {
        counts = 0.0f;
    }
    if (counts > static_cast<float>(kAdcFullScale))
    {
        counts = static_cast<float>(kAdcFullScale);
    }
    const float t = counts / static_cast<float>(kAdcFullScale);
    return -range_N + t * (2.0f * range_N);
}

inline float countsToVolt(std::uint16_t counts)
{
    if (counts > kAdcFullScale)
    {
        counts = kAdcFullScale;
    }
    return (static_cast<float>(counts) / static_cast<float>(kAdcFullScale)) * kVref;
}

} // namespace adc_scaler
