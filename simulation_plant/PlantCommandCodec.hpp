#pragma once

#include "SimPlant.hpp"

#include <string>

namespace simplant
{

// Commands of the M15 simulation plant channel.
// This channel is PC-simulation only and is NOT part of the M12 protocol.
enum class PlantCommandType
{
    Unknown,
    GetState,
    SetLimits,
    TriggerProbe,
    SetEStop
};

// Parsed plant channel request (one JSON object per line).
struct PlantCommand
{
    PlantCommandType type = PlantCommandType::Unknown;

    bool has_x_max = false;
    bool has_y_max = false;
    bool has_z_max = false;
    float x_max = 0.0f;
    float y_max = 0.0f;
    float z_max = 0.0f;

    bool has_pulse_ms = false;
    float pulse_ms = 0.0f;

    bool e_stop_active = false;
};

// Parse one request line. Unknown or malformed input yields type Unknown.
PlantCommand parsePlantCommand(const std::string& line);

// Apply a parsed command to the plant. Returns false for unknown commands.
// Applying never moves the machine; it only changes virtual sensor state.
bool applyPlantCommand(SimPlant& plant, const PlantCommand& command);

// Serialize a plant state plus the Core-reported pose as a JSON response line.
std::string formatPlantStateResponse(const PlantState& state, float x, float y, float z);

// Serialize an error response line.
std::string formatPlantErrorResponse(const std::string& error);

} // namespace simplant
