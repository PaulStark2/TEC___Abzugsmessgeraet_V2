#pragma once

#include "SimPlant.hpp"

#include <string>

namespace simplant
{

enum class PlantCommandType
{
    Unknown,
    GetState,
    SetLimits,
    TriggerProbe,
    SetEStop,
    SetProbeDisc,
    SetForceStiffness,
    SetInjectedForce
};

struct PlantCommand
{
    PlantCommandType type = PlantCommandType::Unknown;

    bool has_x_min = false;
    bool has_x_max = false;
    bool has_y_min = false;
    bool has_y_max = false;
    bool has_z_min = false;
    bool has_z_max = false;
    float x_min = 0.0f;
    float x_max = 0.0f;
    float y_min = 0.0f;
    float y_max = 0.0f;
    float z_min = 0.0f;
    float z_max = 0.0f;

    bool has_pulse_ms = false;
    float pulse_ms = 0.0f;

    bool e_stop_active = false;

    bool has_probe_x = false;
    bool has_probe_y = false;
    bool has_probe_z = false;
    bool has_probe_diameter = false;
    bool has_probe_thickness = false;
    bool has_tool_diameter = false;
    bool has_tool_cylindrical_height = false;
    float probe_x = 0.0f;
    float probe_y = 0.0f;
    float probe_z = 0.0f;
    float probe_diameter = 0.0f;
    float probe_thickness = 0.0f;
    float tool_diameter = 0.0f;
    float tool_cylindrical_height = 0.0f;

    bool has_force_stiffness = false;
    float force_stiffness_n_per_mm = 5.0f;

    bool has_injected_fx = false;
    bool has_injected_fy = false;
    bool has_injected_fz = false;
    float injected_fx = 0.0f;
    float injected_fy = 0.0f;
    float injected_fz = 0.0f;
};

PlantCommand parsePlantCommand(const std::string& line);
bool applyPlantCommand(SimPlant& plant, const PlantCommand& command);
// pose = Core raw TCP (gantry). referenceOffset = Core MCS (display only, not a plant sensor).
std::string formatPlantStateResponse(const PlantState& state, float x, float y, float z);
std::string formatPlantStateResponse(const PlantState& state, float x, float y, float z,
                                     float offset_x, float offset_y, float offset_z);
std::string formatPlantErrorResponse(const std::string& error);

} // namespace simplant
