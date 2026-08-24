#pragma once

#include "MachineConfig.hpp"
#include "Payloads.hpp"
#include "TestConfiguration.hpp"

// Map M12 config payloads to Core structs (bridge only; communication stays Core-free).
inline MachineConfig toMachineConfig(const tecomm::MachineConfigPayload& p)
{
    MachineConfig cfg{};
    cfg.motion.v_approach = p.v_approach;
    cfg.motion.v_slow = p.v_slow;
    cfg.motion.v_measure = p.v_measure;
    cfg.motion.v_homing = p.v_homing;
    cfg.motion.acceleration = p.acceleration;
    cfg.motion.phase1_v_extract = p.phase1_v_extract;
    cfg.motion.phase1_v_insert = p.phase1_v_insert;
    cfg.motion.phase2_v_extract = p.phase2_v_extract;
    cfg.motion.phase2_v_insert = p.phase2_v_insert;
    cfg.motion.phase3_v_extract = p.phase3_v_extract;
    cfg.motion.phase3_v_insert = p.phase3_v_insert;
    cfg.force.force_filter = p.force_filter;
    cfg.force.xy_tolerance_N = p.xy_tolerance_N;
    cfg.force.xy_correction_step = p.xy_correction_step;
    cfg.force.xy_max_correction = p.xy_max_correction;
    cfg.force.insertion_buffer = p.insertion_buffer;
    cfg.collision.approach_N = p.collision_approach_N;
    cfg.collision.retract_N = p.collision_retract_N;
    cfg.collision.clamp_N = p.collision_clamp_N;
    cfg.collision.unclamp_N = p.collision_unclamp_N;
    cfg.collision.contact_N = p.collision_contact_N;
    cfg.collision.measure_N = p.collision_measure_N;
    cfg.spreader.extend_time_s = p.spreader_extend_time_s;
    cfg.spreader.retract_time_s = p.spreader_retract_time_s;
    cfg.phase2.force_tolerance_N = p.force_tolerance_N;
    cfg.phase2.z_correction_gain = p.z_correction_gain;
    cfg.phase2.z_correction_max = p.z_correction_max;
    cfg.phase2.minimum_correction = p.minimum_correction;
    cfg.phase2.max_optimization_iterations = p.max_optimization_iterations;
    cfg.probe.search_speed = p.probe_search_speed;
    cfg.probe.search_distance = p.probe_search_distance;
    cfg.probe.backoff = p.probe_backoff;
    cfg.probe.head_diameter = p.head_diameter;
    cfg.probe.measuring_head_diameter = p.measuring_head_diameter;
    cfg.probe.measuring_head_thickness = p.measuring_head_thickness;
    cfg.probe.tool_diameter = p.tool_diameter;
    cfg.probe.tool_cylindrical_height = p.tool_cylindrical_height;
    cfg.probe.xy_search_margin = p.probe_xy_search_margin;
    cfg.force.insert_search_distance = p.insert_search_distance;
    cfg.probe.search_depth = p.search_depth;
    cfg.probe.approach_distance = p.approach_distance;
    cfg.probe.fine_search_speed = p.fine_search_speed;
    cfg.probe.probe_offset_reference = TestPoint{p.probe_offset_reference_x, p.probe_offset_reference_y,
                                                  p.probe_offset_reference_z};
    cfg.probe.num_fine_searches = p.num_fine_searches;
    cfg.calibration.home_offset = TestPoint{p.home_offset_x, p.home_offset_y, p.home_offset_z};
    cfg.calibration.tool_offset = TestPoint{p.tool_offset_x, p.tool_offset_y, p.tool_offset_z};
    cfg.motion.safe_z_buffer = p.safe_z_buffer;
    cfg.force.sample_timeout_s = p.sample_timeout_s;
    cfg.communication.heartbeat_timeout_s = p.heartbeat_timeout_s;
    cfg.force.xy_correct_timeout_s = p.xy_correct_timeout_s;
    return cfg;
}

inline tecomm::MachineConfigPayload toMachineConfigPayload(const MachineConfig& cfg)
{
    tecomm::MachineConfigPayload p{};
    p.v_approach = cfg.motion.v_approach;
    p.v_slow = cfg.motion.v_slow;
    p.v_measure = cfg.motion.v_measure;
    p.v_homing = cfg.motion.v_homing;
    p.acceleration = cfg.motion.acceleration;
    p.phase1_v_extract = cfg.motion.phase1_v_extract;
    p.phase1_v_insert = cfg.motion.phase1_v_insert;
    p.phase2_v_extract = cfg.motion.phase2_v_extract;
    p.phase2_v_insert = cfg.motion.phase2_v_insert;
    p.phase3_v_extract = cfg.motion.phase3_v_extract;
    p.phase3_v_insert = cfg.motion.phase3_v_insert;
    p.force_filter = cfg.force.force_filter;
    p.xy_tolerance_N = cfg.force.xy_tolerance_N;
    p.xy_correction_step = cfg.force.xy_correction_step;
    p.xy_max_correction = cfg.force.xy_max_correction;
    p.insertion_buffer = cfg.force.insertion_buffer;
    p.collision_approach_N = cfg.collision.approach_N;
    p.collision_retract_N = cfg.collision.retract_N;
    p.collision_clamp_N = cfg.collision.clamp_N;
    p.collision_unclamp_N = cfg.collision.unclamp_N;
    p.collision_contact_N = cfg.collision.contact_N;
    p.collision_measure_N = cfg.collision.measure_N;
    p.spreader_extend_time_s = cfg.spreader.extend_time_s;
    p.spreader_retract_time_s = cfg.spreader.retract_time_s;
    p.force_tolerance_N = cfg.phase2.force_tolerance_N;
    p.z_correction_gain = cfg.phase2.z_correction_gain;
    p.z_correction_max = cfg.phase2.z_correction_max;
    p.minimum_correction = cfg.phase2.minimum_correction;
    p.max_optimization_iterations =
        static_cast<uint16_t>(cfg.phase2.max_optimization_iterations);
    p.probe_search_speed = cfg.probe.search_speed;
    p.probe_search_distance = cfg.probe.search_distance;
    p.probe_backoff = cfg.probe.backoff;
    p.home_offset_x = cfg.calibration.home_offset.x;
    p.home_offset_y = cfg.calibration.home_offset.y;
    p.home_offset_z = cfg.calibration.home_offset.z;
    p.tool_offset_x = cfg.calibration.tool_offset.x;
    p.tool_offset_y = cfg.calibration.tool_offset.y;
    p.tool_offset_z = cfg.calibration.tool_offset.z;
    p.safe_z_buffer = cfg.motion.safe_z_buffer;
    p.sample_timeout_s = cfg.force.sample_timeout_s;
    p.heartbeat_timeout_s = cfg.communication.heartbeat_timeout_s;
    p.xy_correct_timeout_s = cfg.force.xy_correct_timeout_s;
    p.head_diameter = cfg.probe.head_diameter;
    p.measuring_head_diameter = cfg.probe.measuring_head_diameter;
    p.measuring_head_thickness = cfg.probe.measuring_head_thickness;
    p.tool_diameter = cfg.probe.tool_diameter;
    p.tool_cylindrical_height = cfg.probe.tool_cylindrical_height;
    p.probe_xy_search_margin = cfg.probe.xy_search_margin;
    p.insert_search_distance = cfg.force.insert_search_distance;
    p.search_depth = cfg.probe.search_depth;
    p.approach_distance = cfg.probe.approach_distance;
    p.fine_search_speed = cfg.probe.fine_search_speed;
    p.probe_offset_reference_x = cfg.probe.probe_offset_reference.x;
    p.probe_offset_reference_y = cfg.probe.probe_offset_reference.y;
    p.probe_offset_reference_z = cfg.probe.probe_offset_reference.z;
    p.num_fine_searches = static_cast<uint16_t>(cfg.probe.num_fine_searches);
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
