#pragma once

#include "CalibrationSet.hpp"
#include "CoordinateTransform.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"

#include <cmath>

// Dynamic Safe-Z from the loaded test program and optional probe (plate frame):
//   max_clearance_z_plate = max(max(program.point.z), probe_z_plate)
//   safe_z_plate = max_clearance_z_plate + safe_z_buffer
// probe_z_plate = probe_center.z - plate_origin.z - tool_offset.z (machine → plate).
// Probe is included only when probe_center_valid. Machine traverse height:
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

// Inverse of toMachinePoint for Z only: probe_center is stored in machine frame.
inline bool computeProbeZPlate(const CalibrationSet& calibration, float& out_probe_z_plate)
{
    if (!calibration.probe_center_valid || !calibration.plate_origin_valid)
    {
        return false;
    }
    if (!std::isfinite(calibration.probe_center.z) || !std::isfinite(calibration.plate_origin.z) ||
        !std::isfinite(calibration.tool_offset.z))
    {
        return false;
    }
    const float plate =
        calibration.probe_center.z - calibration.plate_origin.z - calibration.tool_offset.z;
    if (!std::isfinite(plate))
    {
        return false;
    }
    out_probe_z_plate = plate;
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

// Plate-frame Safe-Z including a valid Messtaster when it sits higher than the points.
inline bool computeSafeZPlateWithProbe(const TestProgram& program, float safe_z_buffer,
                                       const CalibrationSet& calibration, float& out_max_point_z,
                                       float& out_max_clearance_z, float& out_safe_z_plate)
{
    float max_point_z = 0.0f;
    float points_only_plate = 0.0f;
    if (!computeSafeZPlate(program, safe_z_buffer, max_point_z, points_only_plate))
    {
        return false;
    }
    float max_clearance_z = max_point_z;
    float probe_z_plate = 0.0f;
    if (computeProbeZPlate(calibration, probe_z_plate) && probe_z_plate > max_clearance_z)
    {
        max_clearance_z = probe_z_plate;
    }
    const float plate = max_clearance_z + safe_z_buffer;
    if (!std::isfinite(plate))
    {
        return false;
    }
    (void)points_only_plate;
    out_max_point_z = max_point_z;
    out_max_clearance_z = max_clearance_z;
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
    float max_point_z = 0.0f;
    float max_clearance_z = 0.0f;
    float safe_z_plate = 0.0f;
    if (!computeSafeZPlateWithProbe(program, safe_z_buffer, calibration, max_point_z, max_clearance_z,
                                    safe_z_plate))
    {
        return false;
    }
    (void)max_point_z;
    (void)max_clearance_z;
    return computeSafeZMachine(calibration, safe_z_plate, out_safe_z_machine);
}
