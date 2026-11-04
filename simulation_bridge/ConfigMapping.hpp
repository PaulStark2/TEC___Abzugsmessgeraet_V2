#pragma once



#include "MachineConfig.hpp"

#include "Payloads.hpp"

#include "TestConfiguration.hpp"



// Map M12 config payloads to Core structs (bridge only; communication stays Core-free).

inline MachineConfig toMachineConfig(const tecomm::MachineConfigPayload& p)

{

    MachineConfig cfg{};

    cfg.motion.v_approach_testpoints = p.v_approach_testpoints;

    cfg.motion.v_fine_testpoints = p.v_fine_testpoints;

    cfg.motion.v_bore_search = p.v_bore_search;

    cfg.motion.v_unclamp = p.v_unclamp;

    cfg.motion.v_homing = p.v_homing;

    cfg.motion.v_homing_fast = p.v_homing_fast;

    cfg.motion.homing_approach_distance = p.homing_approach_distance;

    cfg.motion.a_homing_fast = p.a_homing_fast;

    cfg.motion.a_testpoints = p.a_testpoints;

    cfg.motion.corner_testpoints = p.corner_testpoints;

    cfg.motion.a_homing = p.a_homing;

    cfg.motion.a_manual = p.a_manual;

    cfg.motion.phase1_v_extract = p.phase1_v_extract;

    cfg.motion.phase1_v_insert = p.phase1_v_insert;

    cfg.motion.phase1_a = p.phase1_a;

    cfg.motion.phase1_corner = p.phase1_corner;

    cfg.motion.phase3_v_extract = p.phase3_v_extract;

    cfg.motion.phase3_v_insert = p.phase3_v_insert;
    cfg.motion.phase3_v_insert_fine = p.phase3_v_insert_fine;

    cfg.motion.phase3_a = p.phase3_a;

    cfg.motion.phase3_corner = p.phase3_corner;

    cfg.force.force_filter = p.force_filter;

    cfg.force.xy_tolerance_N = p.xy_tolerance_N;

    cfg.force.xy_correction_step = p.xy_correction_step;

    cfg.force.xy_max_correction = p.xy_max_correction;

    cfg.force.insertion_buffer = p.insertion_buffer;

    cfg.force.after_seat_retract_mm = p.after_seat_retract_mm;

    cfg.force.extract_after_free_mm = p.extract_after_free_mm;

    cfg.force.force_path_dz_mm = p.force_path_dz_mm;

    cfg.force.force_path_dz_fine_mm = p.force_path_dz_fine_mm;

    cfg.force.force_near_target_frac = p.force_near_target_frac;

    cfg.motion.extract_after_free_v = p.extract_after_free_v;

    cfg.collision.approach_N = p.collision_approach_N;

    cfg.collision.approach_x_N = p.collision_approach_x_N;

    cfg.collision.approach_y_N = p.collision_approach_y_N;

    cfg.collision.retract_N = p.collision_retract_N;

    cfg.collision.retract_x_N = p.collision_retract_x_N;

    cfg.collision.retract_y_N = p.collision_retract_y_N;

    cfg.collision.clamp_N = p.collision_clamp_N;

    cfg.collision.clamp_x_N = p.collision_clamp_x_N;

    cfg.collision.clamp_y_N = p.collision_clamp_y_N;

    cfg.collision.unclamp_N = p.collision_unclamp_N;

    cfg.collision.unclamp_x_N = p.collision_unclamp_x_N;

    cfg.collision.unclamp_y_N = p.collision_unclamp_y_N;

    cfg.collision.contact_N = p.collision_contact_N;

    cfg.collision.contact_x_N = p.collision_contact_x_N;

    cfg.collision.contact_y_N = p.collision_contact_y_N;

    cfg.collision.measure_N = p.collision_measure_N;

    cfg.collision.measure_x_N = p.collision_measure_x_N;

    cfg.collision.measure_y_N = p.collision_measure_y_N;

    cfg.fault_manual.v_max = p.fault_manual_v_max;

    cfg.fault_manual.a = p.fault_manual_a;

    cfg.fault_manual.hard_limit_x_N = p.fault_manual_hard_limit_x_N;

    cfg.fault_manual.hard_limit_y_N = p.fault_manual_hard_limit_y_N;

    cfg.fault_manual.hard_limit_N = p.fault_manual_hard_limit_N;

    cfg.spreader.extend_time_s = p.spreader_extend_time_s;

    cfg.spreader.retract_time_s = p.spreader_retract_time_s;

    cfg.force.force_tolerance_N = p.force_tolerance_N;

    cfg.probe.v_search = p.v_search;

    cfg.probe.v_approach = p.probe_v_approach;

    cfg.probe.wall_force_N = p.probe_wall_force_N;

    cfg.probe.floor_force_N = p.probe_floor_force_N;

    cfg.probe.xy_search_limit = p.probe_xy_search_limit;

    cfg.probe.z_search_limit = p.probe_z_search_limit;

    cfg.probe.z_lift_mm = p.probe_z_lift_mm;

    cfg.probe.timeout_s = p.probe_bore_timeout_s;

    cfg.probe.refine_passes = p.probe_bore_refine_passes;

    cfg.probe.approach_distance = p.approach_distance;

    cfg.force.insert_search_distance = p.insert_search_distance;

    cfg.calibration.home_offset = TestPoint{p.home_offset_x, p.home_offset_y, p.home_offset_z};

    cfg.calibration.tool_offset = TestPoint{p.tool_offset_x, p.tool_offset_y, p.tool_offset_z};

    cfg.motion.safe_z_buffer = p.safe_z_buffer;

    cfg.motion.safe_z_calib_buffer = p.safe_z_calib_buffer;

    cfg.force.sample_timeout_s = p.sample_timeout_s;

    cfg.communication.heartbeat_timeout_s = p.heartbeat_timeout_s;

    cfg.force.xy_correct_timeout_s = p.xy_correct_timeout_s;

    cfg.force.range_N = p.force_range_N;

    cfg.force.hard_limit_N = p.force_hard_limit_N;

    cfg.force.hard_limit_x_N = p.force_hard_limit_x_N;

    cfg.force.hard_limit_y_N = p.force_hard_limit_y_N;

    cfg.force.xy_search_limit = p.xy_search_limit;

    cfg.force.preclamp_confirm = p.preclamp_confirm != 0;

    cfg.force.bore_refine_passes = p.bore_refine_passes;

    cfg.force.coord_tolerance_mm = p.coord_tolerance_mm;

    cfg.force.test_lateral_force_N = p.test_lateral_force_N;

    cfg.force.z_limit_pos_mm = p.z_limit_pos_mm;

    cfg.force.z_limit_neg_mm = p.z_limit_neg_mm;

    cfg.force.anomaly_skip = p.anomaly_skip != 0;

    cfg.force.tool_offset_change_tolerance_mm = p.tool_offset_change_tolerance_mm;

    return cfg;

}



inline tecomm::MachineConfigPayload toMachineConfigPayload(const MachineConfig& cfg)

{

    tecomm::MachineConfigPayload p{};

    p.v_approach_testpoints = cfg.motion.v_approach_testpoints;

    p.v_fine_testpoints = cfg.motion.v_fine_testpoints;

    p.v_unclamp = cfg.motion.v_unclamp;

    p.v_homing = cfg.motion.v_homing;

    p.v_homing_fast = cfg.motion.v_homing_fast;

    p.homing_approach_distance = cfg.motion.homing_approach_distance;

    p.a_homing_fast = cfg.motion.a_homing_fast;

    p.a_testpoints = cfg.motion.a_testpoints;

    p.corner_testpoints = cfg.motion.corner_testpoints;

    p.a_homing = cfg.motion.a_homing;

    p.a_manual = cfg.motion.a_manual;

    p.phase1_v_extract = cfg.motion.phase1_v_extract;

    p.phase1_v_insert = cfg.motion.phase1_v_insert;

    p.phase1_a = cfg.motion.phase1_a;

    p.phase1_corner = cfg.motion.phase1_corner;

    p.phase3_v_extract = cfg.motion.phase3_v_extract;

    p.phase3_v_insert = cfg.motion.phase3_v_insert;
    p.phase3_v_insert_fine = cfg.motion.phase3_v_insert_fine;

    p.phase3_a = cfg.motion.phase3_a;

    p.phase3_corner = cfg.motion.phase3_corner;

    p.force_filter = cfg.force.force_filter;

    p.xy_tolerance_N = cfg.force.xy_tolerance_N;

    p.xy_correction_step = cfg.force.xy_correction_step;

    p.xy_max_correction = cfg.force.xy_max_correction;

    p.insertion_buffer = cfg.force.insertion_buffer;

    p.after_seat_retract_mm = cfg.force.after_seat_retract_mm;

    p.extract_after_free_mm = cfg.force.extract_after_free_mm;

    p.force_path_dz_mm = cfg.force.force_path_dz_mm;

    p.force_path_dz_fine_mm = cfg.force.force_path_dz_fine_mm;

    p.force_near_target_frac = cfg.force.force_near_target_frac;

    p.extract_after_free_v = cfg.motion.extract_after_free_v;

    p.collision_approach_N = cfg.collision.approach_N;

    p.collision_approach_x_N = cfg.collision.approach_x_N;

    p.collision_approach_y_N = cfg.collision.approach_y_N;

    p.collision_retract_N = cfg.collision.retract_N;

    p.collision_retract_x_N = cfg.collision.retract_x_N;

    p.collision_retract_y_N = cfg.collision.retract_y_N;

    p.collision_clamp_N = cfg.collision.clamp_N;

    p.collision_clamp_x_N = cfg.collision.clamp_x_N;

    p.collision_clamp_y_N = cfg.collision.clamp_y_N;

    p.collision_unclamp_N = cfg.collision.unclamp_N;

    p.collision_unclamp_x_N = cfg.collision.unclamp_x_N;

    p.collision_unclamp_y_N = cfg.collision.unclamp_y_N;

    p.collision_contact_N = cfg.collision.contact_N;

    p.collision_contact_x_N = cfg.collision.contact_x_N;

    p.collision_contact_y_N = cfg.collision.contact_y_N;

    p.collision_measure_N = cfg.collision.measure_N;

    p.collision_measure_x_N = cfg.collision.measure_x_N;

    p.collision_measure_y_N = cfg.collision.measure_y_N;

    p.spreader_extend_time_s = cfg.spreader.extend_time_s;

    p.spreader_retract_time_s = cfg.spreader.retract_time_s;

    p.force_tolerance_N = cfg.force.force_tolerance_N;

    p.v_search = cfg.probe.v_search;

    p.home_offset_x = cfg.calibration.home_offset.x;

    p.home_offset_y = cfg.calibration.home_offset.y;

    p.home_offset_z = cfg.calibration.home_offset.z;

    p.tool_offset_x = cfg.calibration.tool_offset.x;

    p.tool_offset_y = cfg.calibration.tool_offset.y;

    p.tool_offset_z = cfg.calibration.tool_offset.z;

    p.safe_z_buffer = cfg.motion.safe_z_buffer;

    p.safe_z_calib_buffer = cfg.motion.safe_z_calib_buffer;

    p.sample_timeout_s = cfg.force.sample_timeout_s;

    p.heartbeat_timeout_s = cfg.communication.heartbeat_timeout_s;

    p.xy_correct_timeout_s = cfg.force.xy_correct_timeout_s;

    p.approach_distance = cfg.probe.approach_distance;

    p.probe_v_approach = cfg.probe.v_approach;

    p.probe_wall_force_N = cfg.probe.wall_force_N;

    p.probe_floor_force_N = cfg.probe.floor_force_N;

    p.probe_xy_search_limit = cfg.probe.xy_search_limit;

    p.probe_z_search_limit = cfg.probe.z_search_limit;

    p.probe_z_lift_mm = cfg.probe.z_lift_mm;

    p.probe_bore_timeout_s = cfg.probe.timeout_s;

    p.probe_bore_refine_passes = cfg.probe.refine_passes;

    p.insert_search_distance = cfg.force.insert_search_distance;

    // probe_center_* filled by GetMachineConfig from CalibrationSet (not MachineConfig).

    p.probe_center_x = 0.0f;

    p.probe_center_y = 0.0f;

    p.probe_center_z = 0.0f;

    p.force_range_N = cfg.force.range_N;

    p.force_hard_limit_N = cfg.force.hard_limit_N;

    p.force_hard_limit_x_N = cfg.force.hard_limit_x_N;

    p.force_hard_limit_y_N = cfg.force.hard_limit_y_N;

    p.xy_search_limit = cfg.force.xy_search_limit;

    p.preclamp_confirm = cfg.force.preclamp_confirm ? 1 : 0;

    p.v_bore_search = cfg.motion.v_bore_search;

    p.bore_refine_passes = cfg.force.bore_refine_passes;

    p.coord_tolerance_mm = cfg.force.coord_tolerance_mm;

    p.test_lateral_force_N = cfg.force.test_lateral_force_N;

    p.z_limit_pos_mm = cfg.force.z_limit_pos_mm;

    p.z_limit_neg_mm = cfg.force.z_limit_neg_mm;

    p.anomaly_skip = cfg.force.anomaly_skip ? 1 : 0;

    p.tool_offset_change_tolerance_mm = cfg.force.tool_offset_change_tolerance_mm;

    p.fault_manual_v_max = cfg.fault_manual.v_max;

    p.fault_manual_a = cfg.fault_manual.a;

    p.fault_manual_hard_limit_x_N = cfg.fault_manual.hard_limit_x_N;

    p.fault_manual_hard_limit_y_N = cfg.fault_manual.hard_limit_y_N;

    p.fault_manual_hard_limit_N = cfg.fault_manual.hard_limit_N;

    return p;

}



inline TestConfiguration toTestConfiguration(const tecomm::TestConfigurationPayload& p)

{

    TestConfiguration cfg{};

    cfg.mode = (p.mode == 1) ? TestMode::Aging : TestMode::Test;

    cfg.target_force_N = p.target_force_N;

    cfg.iterations = p.iterations;

    cfg.active_point_index = p.active_point_index;

    return cfg;

}



inline tecomm::TestConfigurationPayload toTestConfigurationPayload(const TestConfiguration& cfg)

{

    tecomm::TestConfigurationPayload p{};

    p.mode = (cfg.mode == TestMode::Aging) ? 1 : 0;

    p.target_force_N = cfg.target_force_N;

    p.iterations = static_cast<uint16_t>(cfg.iterations);

    p.active_point_index = cfg.active_point_index;

    return p;

}


