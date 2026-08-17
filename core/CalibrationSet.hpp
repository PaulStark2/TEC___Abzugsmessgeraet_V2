#pragma once

#include "TestPoint.hpp"

// Taught geometry. plateOrigin / probeCenter are overwritten on each teach/calib.
// toolOffset is the fixed mechanical probe-tip → spreader-TCP offset (default 0).
struct CalibrationSet
{
    TestPoint plate_origin{0.0f, 0.0f, 0.0f}; // machine pose of the plate calibration point
    bool plate_origin_valid = false; // false until a teach command succeeds

    TestPoint probe_center{0.0f, 0.0f, 0.0f}; // machine pose of the probe tip center
    bool probe_center_valid = false; // false until ProbeCalibration completes

    TestPoint tool_offset{0.0f, 0.0f, 0.0f}; // probe tip → TCP; initial 0,0,0
    TestPoint home_offset{0.0f, 0.0f, 0.0f}; // pose applied after successful homing
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
