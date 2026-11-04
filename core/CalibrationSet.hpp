#pragma once

#include "TestPoint.hpp"

// Taught geometry.
// plateOrigin (KM): TCP pose at plate center bore teach (plate frame origin).
// probeCenter: durable installed measuring-disc reference (FullCalib Install only).
// toolOffset: TCP/tool deviation vs probeCenter (Procedure OffsetOnly); 0 after Install.
// plate_yaw_rad: Z rotation of the plate frame relative to machine (from K1–K3).
struct CalibrationSet
{
    TestPoint plate_origin{0.0f, 0.0f, 0.0f}; // machine pose of KM
    bool plate_origin_valid = false; // false until a teach command succeeds

    TestPoint probe_center{0.0f, 0.0f, 0.0f}; // installed probe reference (machine frame)
    bool probe_center_valid = false; // false until FullCalib/Geometry Install completes

    // tool_offset = probe_center - measured (OffsetOnly). Zero after Install. Never from
    // legacy probe_offset_reference. Applied as: machine = plate + Rz(yaw)*point + tool_offset.
    TestPoint tool_offset{0.0f, 0.0f, 0.0f};

    // Z yaw of plate axes relative to machine. 0 = axes parallel (legacy behaviour).
    float plate_yaw_rad = 0.0f;

    // Optional measured K1–K3 (machine frame) for diagnostics / reconnect display.
    TestPoint k1{0.0f, 0.0f, 0.0f};
    TestPoint k2{0.0f, 0.0f, 0.0f};
    TestPoint k3{0.0f, 0.0f, 0.0f};
    bool rotation_points_valid = false;

    // Derived MCS home pose: switch = (0,0,0), home = (+Rx,-Ry,-Rz).
    // Not measured. Follows MachineConfig.calibration.home_offset (retract R) so changing R
    // does not require re-defining MCS. Normal home() returns here without redefining MCS.
    TestPoint home_offset{0.0f, 0.0f, 0.0f};

    // True after MCS has been defined once (X at Min, Y/Z at +Max).
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

inline void setPlateYaw(CalibrationSet& set, float yaw_rad)
{
    set.plate_yaw_rad = yaw_rad;
}

inline void setRotationPoints(CalibrationSet& set, const TestPoint& k1, const TestPoint& k2,
                              const TestPoint& k3)
{
    set.k1 = k1;
    set.k2 = k2;
    set.k3 = k3;
    set.rotation_points_valid = true;
}

inline void clearRotationPoints(CalibrationSet& set)
{
    set.k1 = TestPoint{};
    set.k2 = TestPoint{};
    set.k3 = TestPoint{};
    set.rotation_points_valid = false;
    set.plate_yaw_rad = 0.0f;
}
