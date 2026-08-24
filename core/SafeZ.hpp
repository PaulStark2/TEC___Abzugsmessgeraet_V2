#pragma once

#include "CalibrationSet.hpp"
#include "CoordinateTransform.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"

#include <cmath>

// Dynamic Safe-Z from the loaded test program (plate frame):
//   safe_z_plate = max(program.point.z) + safe_z_buffer
// Machine traverse height:
//   safe_z_machine = plate_origin.z + safe_z_plate + tool_offset.z
// Requires a non-empty program and a positive finite buffer. No synthetic default height.

inline bool computeMaxPlatePointZ(const TestProgram& program, float& out_max_z)
{
    if (program.size() == 0)
    {
        return false;
    }
    float max_z = 0.0f;
    bool have = false;
    for (std::size_t i = 0; i < program.size(); ++i)
    {
        TestPoint plate{};
        if (!program.pointAt(i, plate) || !std::isfinite(plate.z))
        {
            return false;
        }
        if (!have || plate.z > max_z)
        {
            max_z = plate.z;
            have = true;
        }
    }
    if (!have)
    {
        return false;
    }
    out_max_z = max_z;
    return true;
}

inline bool computeSafeZPlate(const TestProgram& program, float safe_z_buffer, float& out_max_point_z,
                              float& out_safe_z_plate)
{
    if (!(safe_z_buffer > 0.0f) || !std::isfinite(safe_z_buffer))
    {
        return false;
    }
    float max_z = 0.0f;
    if (!computeMaxPlatePointZ(program, max_z))
    {
        return false;
    }
    const float plate = max_z + safe_z_buffer;
    if (!std::isfinite(plate))
    {
        return false;
    }
    out_max_point_z = max_z;
    out_safe_z_plate = plate;
    return true;
}

inline bool computeSafeZMachine(const CalibrationSet& calibration, float safe_z_plate,
                                float& out_safe_z_machine)
{
    TestPoint plate{0.0f, 0.0f, safe_z_plate};
    TestPoint machine{};
    if (!toMachinePoint(calibration, plate, machine))
    {
        return false;
    }
    if (!std::isfinite(machine.z))
    {
        return false;
    }
    out_safe_z_machine = machine.z;
    return true;
}

inline bool resolveProgramSafeZMachine(const TestProgram& program, float safe_z_buffer,
                                       const CalibrationSet& calibration, float& out_safe_z_machine)
{
    float max_z = 0.0f;
    float safe_z_plate = 0.0f;
    if (!computeSafeZPlate(program, safe_z_buffer, max_z, safe_z_plate))
    {
        return false;
    }
    (void)max_z;
    return computeSafeZMachine(calibration, safe_z_plate, out_safe_z_machine);
}
