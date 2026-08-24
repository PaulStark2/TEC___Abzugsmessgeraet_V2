#pragma once

#include "SafetyLimits.hpp"

#include <cmath>

namespace simplant
{

// 1D process force (Fx=Fy=0). Sign matches Core:
//   InsertInitial unload uses |Fz| for force-free, insert uses Fz >= target_force_N.
// At the test point after the spreader couples: Fz = -target (tension).
// Extracting +Z unloads toward 0. force_free_z = z_test + target/k.
// After unload, z < force_free_z produces compression Fz = k * (force_free_z - z).

inline float processForceFreeZ(float z_test, float target_force_N, float k_n_per_mm)
{
    if (!(k_n_per_mm > 0.0f) || !(target_force_N > 0.0f))
    {
        return z_test;
    }
    return z_test + target_force_N / k_n_per_mm;
}

inline float processFzZ(float z, float z_test, float target_force_N, float k_n_per_mm, bool coupled,
                        bool unloaded)
{
    if (!coupled || !(k_n_per_mm > 0.0f))
    {
        return 0.0f;
    }

    const float z_free = processForceFreeZ(z_test, target_force_N, k_n_per_mm);
    float fz = 0.0f;
    if (!unloaded)
    {
        fz = -target_force_N + k_n_per_mm * (z - z_test);
        if (fz > 0.0f)
        {
            fz = 0.0f;
        }
    }
    else if (z < z_free)
    {
        fz = k_n_per_mm * (z_free - z);
    }

    if (fz > SafetyLimits::kMaxFzAbs_N)
    {
        fz = SafetyLimits::kMaxFzAbs_N;
    }
    if (fz < -SafetyLimits::kMaxFzAbs_N)
    {
        fz = -SafetyLimits::kMaxFzAbs_N;
    }
    return fz;
}

inline bool processForceShouldMarkUnloaded(float z, float z_test, float target_force_N, float k_n_per_mm,
                                           float force_tolerance_N)
{
    const float fz = processFzZ(z, z_test, target_force_N, k_n_per_mm, true, false);
    return std::fabs(fz) <= force_tolerance_N;
}

} // namespace simplant
