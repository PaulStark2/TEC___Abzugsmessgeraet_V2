#pragma once

#include "CalibrationSet.hpp"
#include "TestPoint.hpp"

#include <cmath>

// Plate-frame → machine-frame.
// machine.xy = plateOrigin.xy + Rz(plate_yaw) * platePoint.xy + toolOffset.xy
// machine.z  = plateOrigin.z  + platePoint.z + toolOffset.z
// plate_yaw_rad == 0 keeps the historical translation-only behaviour.
inline bool toMachinePoint(const CalibrationSet& calibration, const TestPoint& plate_point,
                           TestPoint& machine_point_out)
{
    if (!calibration.plate_origin_valid)
    {
        return false;
    }
    const float c = std::cos(calibration.plate_yaw_rad);
    const float s = std::sin(calibration.plate_yaw_rad);
    const float rx = c * plate_point.x - s * plate_point.y;
    const float ry = s * plate_point.x + c * plate_point.y;
    machine_point_out.x = calibration.plate_origin.x + rx + calibration.tool_offset.x;
    machine_point_out.y = calibration.plate_origin.y + ry + calibration.tool_offset.y;
    machine_point_out.z =
        calibration.plate_origin.z + plate_point.z + calibration.tool_offset.z;
    return true;
}
