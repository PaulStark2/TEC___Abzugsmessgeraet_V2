#pragma once

#include "ProtocolSpec.hpp"
#include "ProtocolTypes.hpp"

namespace tecomm
{

// Protocol-native payload types (logical/API view; wire layout encoded explicitly).

struct Point3f
{
    float x;
    float y;
    float z;
};

struct MoveToPayload
{
    float x;
    float y;
    float z;
};

struct BoolPayload
{
    uint8_t value; // 0 = false, 1 = true
};

struct LoadProgramPayload
{
    uint8_t point_count; // 0..kLoadProgramMaxPointCount
    Point3f points[kLoadProgramMaxPointCount];
};

// Mirrors current Core HostInputs force + limits + e_stop and HostOutputs pneumatics.
struct StatusReportPayload
{
    ProtocolMachineState machine_state;
    ProtocolMotionState motion_state;
    ProtocolProcessState process_state;
    float x;
    float y;
    float z;
    float fx;
    float fy;
    float fz;
    uint8_t limits; // bitmask: see kLimitBit* in ProtocolTypes.hpp
    uint8_t e_stop; // 0/1
    uint8_t spreader_extend;
    uint8_t spreader_extended;
    uint8_t fixture_extend;
    uint8_t fixture_extended;
    uint8_t current_point_index; // kNoCurrentPointIndex if n/a
    uint8_t program_point_count;
};

struct ProgramSummaryPayload
{
    uint8_t point_count;
    uint8_t current_index;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    float z_min;
    float z_max;
};

struct AckPayload
{
    uint8_t ref_message_id;
    uint8_t result_code; // AckResultCode
};

struct NackPayload
{
    uint8_t ref_message_id;
    uint8_t error_code; // NackErrorCode
    uint8_t detail;
};

} // namespace tecomm
