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
    SetEStop
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
};

PlantCommand parsePlantCommand(const std::string& line);
bool applyPlantCommand(SimPlant& plant, const PlantCommand& command);
std::string formatPlantStateResponse(const PlantState& state, float x, float y, float z);
std::string formatPlantErrorResponse(const std::string& error);

} // namespace simplant
