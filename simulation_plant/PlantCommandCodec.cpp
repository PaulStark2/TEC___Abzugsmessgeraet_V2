#include "PlantCommandCodec.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace simplant
{
namespace
{

// Locate the value position that follows "key" : in a flat JSON object.
bool findValue(const std::string& text, const char* key, std::size_t& out_pos)
{
    const std::string quoted = std::string("\"") + key + "\"";
    const std::size_t key_pos = text.find(quoted);
    if (key_pos == std::string::npos)
    {
        return false;
    }

    std::size_t pos = text.find(':', key_pos + quoted.size());
    if (pos == std::string::npos)
    {
        return false;
    }

    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
    {
        ++pos;
    }
    if (pos >= text.size())
    {
        return false;
    }

    out_pos = pos;
    return true;
}

bool extractString(const std::string& text, const char* key, std::string& out)
{
    std::size_t pos = 0;
    if (!findValue(text, key, pos) || text[pos] != '"')
    {
        return false;
    }

    const std::size_t end = text.find('"', pos + 1);
    if (end == std::string::npos)
    {
        return false;
    }

    out = text.substr(pos + 1, end - pos - 1);
    return true;
}

bool extractNumber(const std::string& text, const char* key, float& out)
{
    std::size_t pos = 0;
    if (!findValue(text, key, pos))
    {
        return false;
    }

    const char* begin = text.c_str() + pos;
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end == begin)
    {
        return false;
    }

    out = value;
    return true;
}

bool extractBool(const std::string& text, const char* key, bool& out)
{
    std::size_t pos = 0;
    if (!findValue(text, key, pos))
    {
        return false;
    }

    if (text.compare(pos, 4, "true") == 0)
    {
        out = true;
        return true;
    }
    if (text.compare(pos, 5, "false") == 0)
    {
        out = false;
        return true;
    }

    // Tolerate numeric booleans (0/1) from simple clients.
    float numeric = 0.0f;
    if (extractNumber(text, key, numeric))
    {
        out = numeric != 0.0f;
        return true;
    }
    return false;
}

std::string formatFloat(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f", static_cast<double>(value));
    return std::string(buffer);
}

const char* boolLiteral(bool value)
{
    return value ? "true" : "false";
}

} // namespace

PlantCommand parsePlantCommand(const std::string& line)
{
    PlantCommand command{};

    std::string name;
    if (!extractString(line, "cmd", name))
    {
        return command;
    }

    if (name == "getState")
    {
        command.type = PlantCommandType::GetState;
        return command;
    }

    if (name == "setLimits")
    {
        command.type = PlantCommandType::SetLimits;
        command.has_x_min = extractNumber(line, "xMin", command.x_min);
        command.has_x_max = extractNumber(line, "xMax", command.x_max);
        command.has_y_min = extractNumber(line, "yMin", command.y_min);
        command.has_y_max = extractNumber(line, "yMax", command.y_max);
        command.has_z_min = extractNumber(line, "zMin", command.z_min);
        command.has_z_max = extractNumber(line, "zMax", command.z_max);
        return command;
    }

    if (name == "triggerProbe")
    {
        command.type = PlantCommandType::TriggerProbe;
        command.has_pulse_ms = extractNumber(line, "pulseMs", command.pulse_ms);
        return command;
    }

    if (name == "setEStop")
    {
        command.type = PlantCommandType::SetEStop;
        if (!extractBool(line, "active", command.e_stop_active))
        {
            command.type = PlantCommandType::Unknown;
        }
        return command;
    }

    if (name == "setProbeDisc")
    {
        command.type = PlantCommandType::SetProbeDisc;
        command.has_probe_x = extractNumber(line, "x", command.probe_x);
        command.has_probe_y = extractNumber(line, "y", command.probe_y);
        command.has_probe_z = extractNumber(line, "z", command.probe_z);
        command.has_probe_diameter = extractNumber(line, "diameter", command.probe_diameter);
        command.has_probe_thickness = extractNumber(line, "thickness", command.probe_thickness);
        command.has_tool_diameter = extractNumber(line, "toolDiameter", command.tool_diameter);
        command.has_tool_cylindrical_height =
            extractNumber(line, "toolCylindricalHeight", command.tool_cylindrical_height);
        return command;
    }

    return command;
}

bool applyPlantCommand(SimPlant& plant, const PlantCommand& command)
{
    switch (command.type)
    {
    case PlantCommandType::GetState:
        return true;

    case PlantCommandType::SetLimits:
    {
        PlantLimits limits = plant.limits();
        if (command.has_x_min)
        {
            limits.x_min = command.x_min;
        }
        if (command.has_x_max)
        {
            limits.x_max = command.x_max;
        }
        if (command.has_y_min)
        {
            limits.y_min = command.y_min;
        }
        if (command.has_y_max)
        {
            limits.y_max = command.y_max;
        }
        if (command.has_z_min)
        {
            limits.z_min = command.z_min;
        }
        if (command.has_z_max)
        {
            limits.z_max = command.z_max;
        }
        plant.setLimits(limits);
        return true;
    }

    case PlantCommandType::TriggerProbe:
        if (command.has_pulse_ms)
        {
            plant.setProbePulseSeconds(command.pulse_ms / 1000.0f);
        }
        plant.triggerProbe();
        return true;

    case PlantCommandType::SetEStop:
        plant.setEStop(command.e_stop_active);
        return true;

    case PlantCommandType::SetProbeDisc:
    {
        ProbeDisc disc = plant.probeDisc();
        if (command.has_probe_x)
        {
            disc.center.x = command.probe_x;
        }
        if (command.has_probe_y)
        {
            disc.center.y = command.probe_y;
        }
        if (command.has_probe_z)
        {
            disc.center.z = command.probe_z;
        }
        if (command.has_probe_diameter)
        {
            disc.diameter = command.probe_diameter;
        }
        if (command.has_probe_thickness)
        {
            disc.thickness = command.probe_thickness;
        }
        if (command.has_tool_diameter)
        {
            disc.tool_diameter = command.tool_diameter;
        }
        if (command.has_tool_cylindrical_height)
        {
            disc.tool_cylindrical_height = command.tool_cylindrical_height;
        }
        plant.setProbeDisc(disc);
        return true;
    }

    case PlantCommandType::Unknown:
    default:
        return false;
    }
}

std::string formatPlantStateResponse(const PlantState& state, float x, float y, float z)
{
    std::string json = "{\"ok\":true,\"state\":{";
    json += "\"limits\":{\"xMin\":" + formatFloat(state.limits.x_min);
    json += ",\"xMax\":" + formatFloat(state.limits.x_max);
    json += ",\"yMin\":" + formatFloat(state.limits.y_min);
    json += ",\"yMax\":" + formatFloat(state.limits.y_max);
    json += ",\"zMin\":" + formatFloat(state.limits.z_min);
    json += ",\"zMax\":" + formatFloat(state.limits.z_max) + "}";
    json += ",\"limitXMin\":";
    json += boolLiteral(state.limit_x_min);
    json += ",\"limitXMax\":";
    json += boolLiteral(state.limit_x_max);
    json += ",\"limitYMin\":";
    json += boolLiteral(state.limit_y_min);
    json += ",\"limitYMax\":";
    json += boolLiteral(state.limit_y_max);
    json += ",\"limitZMin\":";
    json += boolLiteral(state.limit_z_min);
    json += ",\"limitZMax\":";
    json += boolLiteral(state.limit_z_max);
    json += ",\"probeTriggered\":";
    json += boolLiteral(state.probe_triggered);
    json += ",\"probePulseSeconds\":" + formatFloat(state.probe_pulse_seconds);
    json += ",\"probe\":{\"x\":" + formatFloat(state.probe.center.x);
    json += ",\"y\":" + formatFloat(state.probe.center.y);
    json += ",\"z\":" + formatFloat(state.probe.center.z);
    json += ",\"diameter\":" + formatFloat(state.probe.diameter);
    json += ",\"thickness\":" + formatFloat(state.probe.thickness);
    json += ",\"toolDiameter\":" + formatFloat(state.probe.tool_diameter);
    json += ",\"toolCylindricalHeight\":" + formatFloat(state.probe.tool_cylindrical_height);
    json += ",\"triggerSequence\":" + std::to_string(state.probe_trigger_sequence);
    json += ",\"triggerX\":" + formatFloat(state.probe_trigger_x);
    json += ",\"triggerY\":" + formatFloat(state.probe_trigger_y);
    json += ",\"triggerZ\":" + formatFloat(state.probe_trigger_z) + "}";
    json += ",\"eStop\":";
    json += boolLiteral(state.e_stop);
    json += "},\"pose\":{\"x\":" + formatFloat(x);
    json += ",\"y\":" + formatFloat(y);
    json += ",\"z\":" + formatFloat(z) + "}}";
    return json;
}

std::string formatPlantErrorResponse(const std::string& error)
{
    return "{\"ok\":false,\"error\":\"" + error + "\"}";
}

} // namespace simplant
