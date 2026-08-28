#pragma once

#include "TestPoint.hpp"

// Taught geometry.
// plateOrigin: TCP pose at plate teach (plate frame origin).
// probeCenter: durable installed measuring-disc reference (FullCalib Install only).
// toolOffset: TCP/tool deviation vs probeCenter (Procedure OffsetOnly); 0 after Install.
struct CalibrationSet
{
    TestPoint plate_origin{0.0f, 0.0f, 0.0f}; // machine pose of the plate calibration point
    bool plate_origin_valid = false; // false until a teach command succeeds

    TestPoint probe_center{0.0f, 0.0f, 0.0f}; // installed probe reference (machine frame)
    bool probe_center_valid = false; // false until FullCalib/Geometry Install completes

    // tool_offset = probe_center - measured (OffsetOnly). Zero after Install. Never from
    // legacy probe_offset_reference. Applied as: machine = plate + point + tool_offset.
    TestPoint tool_offset{0.0f, 0.0f, 0.0f};

    // Derived MCS home pose: +Max switch = (0,0,0), home = (-Rx,-Ry,-Rz).
    // Not measured. Follows MachineConfig.calibration.home_offset (retract R) so changing R
    // does not require re-defining MCS. Normal home() returns here without redefining MCS.
    TestPoint home_offset{0.0f, 0.0f, 0.0f};

    // True after MCS has been defined once (origin at +max switches).
    // Normal home() re-applies this frame at the switches.
    bool machine_coordinate_system_valid = false;
};

// Overwrite plate origin with the given machine pose and mark it valid.
inline void setPlateOrigin(CalibrationSet& set, const TestPoint& machine_pose)
{
    set.plate_origin = machine_pose;
    set.plate_origin_valid = true;
}

// Overwrite probe center with the given machine pose and mark it valid.
inline void setProbeCenter(CalibrationSet& set, const TestPoint& machine_pose)
{
    set.probe_center = machine_pose;
    set.probe_center_valid = true;
}
