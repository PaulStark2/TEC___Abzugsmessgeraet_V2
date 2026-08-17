#pragma once

#include "CalibrationSet.hpp"
#include "TestPoint.hpp"

// Plate-frame → machine-frame. Axes are parallel.
// machine = plateOrigin + platePoint + toolOffset
inline bool toMachinePoint(const CalibrationSet& calibration, const TestPoint& plate_point,
                           TestPoint& machine_point_out)
{
    if (!calibration.plate_origin_valid)
    {
        return false;
    }
    machine_point_out.x =
        calibration.plate_origin.x + plate_point.x + calibration.tool_offset.x;
    machine_point_out.y =
        calibration.plate_origin.y + plate_point.y + calibration.tool_offset.y;
    machine_point_out.z =
        calibration.plate_origin.z + plate_point.z + calibration.tool_offset.z;
    return true;
}
