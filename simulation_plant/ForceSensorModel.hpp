#pragma once

#include <algorithm>
#include <cstdint>

namespace simplant
{

// Approximate Z-dose used only by the PC simulation so the wizard can run
// without hardware. Three measured anchors: -25 N → 0.3 V, 0 N → 1.5 V,
// +25 N → 2.6 V. The quadratic through those anchors is
// V = 1.5 + 0.046*F - 0.00008*F^2, with F the sensor-frame force in newtons.
inline uint16_t syntheticFzCounts(float sensor_force_n)
{
    const float voltage = 1.5f + 0.046f * sensor_force_n - 0.00008f * sensor_force_n * sensor_force_n;
    const float clamped = std::clamp(voltage, 0.0f, 3.3f);
    const float counts = clamped / 3.3f * 4095.0f;
    return static_cast<uint16_t>(counts + 0.5f);
}

} // namespace simplant
