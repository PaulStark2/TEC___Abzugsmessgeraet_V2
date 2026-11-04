#pragma once

#include "TestPoint.hpp"

#include <cmath>

// tool_offset = P_probe_nominal - P_probe_measured (replace, do not accumulate).
inline bool computeToolOffset(const TestPoint& nominal, const TestPoint& measured, TestPoint& offset_out)
{
    if (!std::isfinite(nominal.x) || !std::isfinite(nominal.y) || !std::isfinite(nominal.z) ||
        !std::isfinite(measured.x) || !std::isfinite(measured.y) || !std::isfinite(measured.z))
    {
        return false;
    }
    offset_out.x = nominal.x - measured.x;
    offset_out.y = nominal.y - measured.y;
    offset_out.z = nominal.z - measured.z;
    return true;
}
