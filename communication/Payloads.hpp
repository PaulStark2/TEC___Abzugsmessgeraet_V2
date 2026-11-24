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
    uint8_t fault_manual; // 0/1 — restricted jog armed while Fault
    uint16_t abort_reason; // Core AbortReason ordinal (soft procedure abort)
    uint16_t current_iteration; // ProcedureController TestOrAge cycle index (0-based)
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
    float v_search;
    float home_offset_x;
    float home_offset_y;
    float home_offset_z;
    float tool_offset_x;
    float tool_offset_y;
    float tool_offset_z;
    float safe_z_buffer;
    float safe_z_calib_buffer = 30.0f;
    float point_z_clearance_mm = 2.0f;
    float sample_timeout_s;
    float heartbeat_timeout_s;
    float xy_correct_timeout_s;
    float approach_distance;
    float probe_v_approach = 10.0f;
    float probe_wall_force_N = 0.5f;
    float probe_floor_force_N = 0.5f;
    float probe_xy_search_limit = 5.0f;
    float probe_z_search_limit = 20.0f;
    float probe_z_lift_mm = 2.0f;
    float probe_bore_timeout_s = 60.0f;
    uint8_t probe_bore_refine_passes = 0;
    float insert_search_distance;
    float phase1_v_extract;
    float phase1_v_insert;
    float phase1_a;
    float phase1_corner;
    float phase3_v_extract;
    float phase3_v_insert;
    float phase3_a;
    float phase3_corner;
    float probe_center_x;
    float probe_center_y;
    float probe_center_z;
    float force_range_N = 30.0f;
    float force_hard_limit_N = 30.0f;
    float a_manual = 500.0f;
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
    // Measure / Phase-3 collision as percent of Prüfkraft (historic *_N wire names).
    float collision_measure_x_N = 150.0f;
    float collision_measure_y_N = 150.0f;
    float xy_search_limit = 5.0f;
    uint8_t preclamp_confirm = 1;
    float v_bore_search = 1.0f;
    uint8_t bore_refine_passes = 0;
    float coord_tolerance_mm = 0.5f;
    float test_lateral_force_N = 5.0f;
    float z_limit_pos_mm = 10.0f;
    float z_limit_neg_mm = 15.0f;
    uint8_t anomaly_skip = 0;
    float tool_offset_change_tolerance_mm = 0.5f;
    float fault_manual_v_max = 5.0f;
    float fault_manual_a = 500.0f;
    float fault_manual_hard_limit_x_N = 50.0f;
    float fault_manual_hard_limit_y_N = 50.0f;
    float fault_manual_hard_limit_N = 50.0f;
    // AfterSeat fixed +Z retract from seat pose [mm]; not program-Z + standoff.
    float after_seat_retract_mm = 2.0f;
    // Extra +Z after extract kraftfrei (post-tensile) [mm]; 0 = stop at kraftfrei.
    float extract_after_free_mm = 0.0f;
    // Speed for that optional after-free +Z segment [mm/s]; independent of phase3_v_extract.
    float extract_after_free_v = 1.0f;
    // Reference homing: fast targeted travel toward home standoff [mm/s].
    float v_homing_fast = 18.0f;
    // Distance before derived home where slow endstop seek begins [mm].
    float homing_approach_distance = 50.0f;
    // Acceleration for that fast approach only [mm/s²]; 0 = constant velocity.
    float a_homing_fast = 500.0f;
    // Kraft-Weg |Δz| sample gate [mm]; 0 = every tick.
    float force_path_dz_mm = 0.01f;
    // Finer |Δz| gate during insert near Sollkraft [mm].
    float force_path_dz_fine_mm = 0.005f;
    // Enter insert fine phase when Fz >= frac · target_force_N (0…1].
    float force_near_target_frac = 0.8f;
    // Insert speed in fine phase [mm/s].
    float phase3_v_insert_fine = 0.5f;
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

struct BoreCenterPayload
{
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
    float residual = 0.0f;
    float fx = 0.0f;
    float fy = 0.0f;
    float x[4] = {};
    float y[4] = {};
    uint8_t anomaly = 0;
    uint8_t confirm_pending = 0;
    uint8_t valid = 0;
    // Appended: Z after floor lift; point_id 0=PreClamp, 1=KM, 2=K1, 3=K2, 4=K3.
    float cz = 0.0f;
    uint8_t point_id = 0;
};

struct PlateYawPayload
{
    float yaw_rad = 0.0f;
};

struct PlateBorePointPayload
{
    uint8_t point_id = 0;
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
    uint8_t leg = 0; // 0 = Insert (Eingliedern), 1 = Extract (Ausgliedern)
    float z[kForcePathChunkMaxSamples];
    float fz[kForcePathChunkMaxSamples];
};

// Operator decision for a procedure anomaly confirm dialog.
struct ProcedureDecisionPayload
{
    uint8_t action = 0; // 0=Continue, 1=NextPoint, 2=RepeatPoint
};

// Procedure anomaly notify (CoordDrift / LateralForce / ZTravelLimit / TcpHang).
struct ProcedureAnomalyPayload
{
    uint8_t type = 0; // 0=CoordDrift, 1=LateralForce, 2=ZTravelLimit, 3=TcpHang
    uint8_t point_index = 0;
    uint16_t iteration = 0;
    float measured = 0.0f;
    float limit = 0.0f;
    float z = 0.0f;
    float fx = 0.0f;
    float fy = 0.0f;
    uint8_t skip_applied = 0;
    uint8_t confirm_pending = 0;
};

} // namespace tecomm
