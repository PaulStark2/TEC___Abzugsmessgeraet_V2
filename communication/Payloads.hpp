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
    uint8_t homing_state; // ProtocolHomingState
    uint8_t blocked_directions; // same bit order as limits
    uint8_t flags; // kStatusFlag*
    uint8_t procedure_phase; // Core ProcedurePhase ordinal
    uint8_t fault_reason; // Core FaultReason ordinal
    uint8_t spreader_state; // Core SpreaderState ordinal
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

// Mirrors Core MachineConfig field order. Communication has no Core dependency.
struct MachineConfigPayload
{
    float v_approach_testpoints;
    float v_fine_testpoints;
    float v_unclamp;
    float v_homing;
    float a_testpoints;
    float corner_testpoints;
    float a_homing;
    float force_filter;
    float xy_tolerance_N;
    float xy_correction_step;
    float xy_max_correction;
    float insertion_buffer;
    float collision_approach_N;
    float collision_retract_N;
    float collision_clamp_N;
    float collision_unclamp_N;
    float collision_contact_N;
    float collision_measure_N;
    float spreader_extend_time_s;
    float spreader_retract_time_s;
    float force_tolerance_N;
    float z_correction_gain;
    float z_correction_max;
    float minimum_correction;
    uint16_t max_optimization_iterations;
    float v_search;
    float probe_search_distance;
    float probe_backoff;
    float home_offset_x;
    float home_offset_y;
    float home_offset_z;
    float tool_offset_x;
    float tool_offset_y;
    float tool_offset_z;
    float safe_z_buffer;
    float sample_timeout_s;
    float heartbeat_timeout_s;
    float xy_correct_timeout_s;
    float head_diameter;
    float measuring_head_diameter;
    float measuring_head_thickness;
    float tool_diameter;
    float search_depth;
    float approach_distance;
    float v_search_fine;
    // Fixed reference used exclusively for legacy tool_offset definition in older builds.
    // Must never be used as a motion/search target or durable probe position.
    float probe_offset_reference_x;
    float probe_offset_reference_y;
    float probe_offset_reference_z;
    float tool_cylindrical_height;
    float probe_xy_search_margin;
    float insert_search_distance;
    uint16_t num_fine_searches;
    float phase1_v_extract;
    float phase1_v_insert;
    float phase1_a;
    float phase1_corner;
    float phase2_v_extract;
    float phase2_v_insert;
    float phase2_a;
    float phase2_corner;
    float phase3_v_extract;
    float phase3_v_insert;
    float phase3_a;
    float phase3_corner;
    float v_approach_probe;
    float v_transfer_probe;
    float a_probe_transfer;
    float corner_probe_transfer;
    float a_probe_search;
    float a_probe_brake;
    // Mirrored from CalibrationSet on GetMachineConfig (Install reference). Ignored on Set.
    float probe_center_x;
    float probe_center_y;
    float probe_center_z;
    // Measuring range and watchdog hard limit [N]. Both default to 30.
    float force_range_N = 30.0f;
    float force_hard_limit_N = 30.0f;
    // Manual jog / hold / calibration acceleration [mm/s^2]. Appended so older fields stay put.
    float a_manual = 500.0f;
    // Per-axis hard limit and collision limits. The unsuffixed fields above are Z.
    float force_hard_limit_x_N = 30.0f;
    float force_hard_limit_y_N = 30.0f;
    float collision_approach_x_N = 20.0f;
    float collision_approach_y_N = 20.0f;
    float collision_retract_x_N = 30.0f;
    float collision_retract_y_N = 30.0f;
    float collision_clamp_x_N = 20.0f;
    float collision_clamp_y_N = 20.0f;
    float collision_unclamp_x_N = 20.0f;
    float collision_unclamp_y_N = 20.0f;
    float collision_contact_x_N = 30.0f;
    float collision_contact_y_N = 30.0f;
    float collision_measure_x_N = 30.0f;
    float collision_measure_y_N = 30.0f;
};

struct TestConfigurationPayload
{
    uint8_t mode; // 0 = Test, 1 = Aging
    float target_force_N;
    uint16_t iterations;
    uint8_t active_point_index;
};

struct ForceCalibPointPayload
{
    uint16_t adc_net = 0;
    uint8_t direction = 0;
    float mass_g = 0.0f;
    float nominal_g = 0.0f;
};

struct SetForceCalibrationPayload
{
    uint8_t source = 0;
    uint8_t confirm = 0;
    uint8_t commit = 0;
    uint8_t count = 0;
    ForceCalibPointPayload points[kForceCalibMaxPoints]{};
};

struct SelectForceCurvePayload
{
    uint8_t source = 0;
    uint8_t confirm = 0;
    uint8_t unit = 0;
    uint8_t commit = 0;
    float beta0 = 0.0f;
    float beta1 = 0.0f;
    float beta2 = 0.0f;
};

struct RawAdcPayload
{
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t z = 0;
    uint8_t valid = 0;
};

struct ForceCalibrationResultPayload
{
    uint8_t requested_source = 0;
    uint8_t resolved_source = 0;
    uint8_t committed = 0;
    uint8_t reason = 0;
    uint8_t degree = 0;
    uint8_t needs_confirmation = 0;
    uint8_t worst_index = 0xFF;
    uint8_t point_count = 0;
    float beta0 = 0.0f;
    float beta1 = 0.0f;
    float beta2 = 0.0f;
    float pearson_r = 0.0f;
    float r_squared = 0.0f;
    float max_abs_residual_N = 0.0f;
    float mean_residual_tension_N = 0.0f;
    float mean_residual_compression_N = 0.0f;
    float linear_max_abs_N = 0.0f;
    float quadratic_max_abs_N = 0.0f;
    float linear_r2 = 0.0f;
    float quadratic_r2 = 0.0f;
    float linear_beta0 = 0.0f;
    float linear_beta1 = 0.0f;
    float linear_beta2 = 0.0f;
    float linear_valid = 0.0f;
    float quadratic_beta0 = 0.0f;
    float quadratic_beta1 = 0.0f;
    float quadratic_beta2 = 0.0f;
    float quadratic_valid = 0.0f;
    float reference_force_N[kForceCalibMaxPoints]{};
};

struct ForcePathChunkPayload
{
    uint8_t point_index;
    uint16_t iteration;
    uint32_t sequence;
    uint8_t sample_count;
    uint32_t dropped;
    float z[kForcePathChunkMaxSamples];
    float fz[kForcePathChunkMaxSamples];
};

} // namespace tecomm
