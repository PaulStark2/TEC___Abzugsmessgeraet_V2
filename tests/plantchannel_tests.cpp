// Unit tests for the M15 simulation plant channel codec (no sockets involved).
#include "PlantCommandCodec.hpp"
#include "SimPlant.hpp"

#include <cmath>
#include <string>

using simplant::PlantCommand;
using simplant::PlantCommandType;
using simplant::SimPlant;

static bool nearEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

static bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

static int testParseUnknown()
{
    if (simplant::parsePlantCommand("").type != PlantCommandType::Unknown)
    {
        return 1;
    }
    if (simplant::parsePlantCommand("{\"cmd\":\"moveTo\",\"x\":10}").type != PlantCommandType::Unknown)
    {
        return 2;
    }
    // Machine commands must never be accepted on the simulation channel.
    SimPlant plant;
    if (simplant::applyPlantCommand(plant, simplant::parsePlantCommand("{\"cmd\":\"moveTo\"}")))
    {
        return 3;
    }
    return 0;
}

static int testSetLimits()
{
    const PlantCommand command =
        simplant::parsePlantCommand("{\"cmd\":\"setLimits\",\"xMax\":12.5,\"zMax\":-3}");
    if (command.type != PlantCommandType::SetLimits)
    {
        return 4;
    }
    if (!command.has_x_max || command.has_y_max || !command.has_z_max)
    {
        return 5;
    }

    SimPlant plant;
    const float previous_y = plant.limits().y_max;
    if (!simplant::applyPlantCommand(plant, command))
    {
        return 6;
    }
    if (!nearEqual(plant.limits().x_max, 12.5f) || !nearEqual(plant.limits().z_max, -3.0f))
    {
        return 7;
    }
    // Omitted fields keep their previous value.
    if (!nearEqual(plant.limits().y_max, previous_y))
    {
        return 8;
    }
    return 0;
}

static int testTriggerProbe()
{
    const PlantCommand command =
        simplant::parsePlantCommand("{\"cmd\":\"triggerProbe\",\"pulseMs\":200}");
    if (command.type != PlantCommandType::TriggerProbe || !command.has_pulse_ms)
    {
        return 9;
    }

    SimPlant plant;
    if (!simplant::applyPlantCommand(plant, command))
    {
        return 10;
    }
    if (!plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 11;
    }
    plant.advance(0.15f);
    if (!plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 12;
    }
    plant.advance(0.06f);
    if (plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 13;
    }
    return 0;
}

static int testSetEStop()
{
    SimPlant plant;
    if (!simplant::applyPlantCommand(
            plant, simplant::parsePlantCommand("{\"cmd\":\"setEStop\",\"active\":true}")))
    {
        return 14;
    }
    if (!plant.eStop())
    {
        return 15;
    }
    if (!simplant::applyPlantCommand(
            plant, simplant::parsePlantCommand("{\"cmd\":\"setEStop\",\"active\":false}")))
    {
        return 16;
    }
    if (plant.eStop())
    {
        return 17;
    }
    // Missing payload must be rejected instead of silently clearing the input.
    if (simplant::applyPlantCommand(plant, simplant::parsePlantCommand("{\"cmd\":\"setEStop\"}")))
    {
        return 18;
    }
    return 0;
}

static int testResponseFormat()
{
    SimPlant plant;
    simplant::PlantLimits limits{};
    limits.x_max = 10.0f;
    plant.setLimits(limits);
    plant.triggerProbe();

    const std::string response = simplant::formatPlantStateResponse(plant.state(11.0f, 1.0f, 2.0f),
                                                                    11.0f, 1.0f, 2.0f);
    if (!contains(response, "\"ok\":true"))
    {
        return 19;
    }
    if (!contains(response, "\"limitXMax\":true") || !contains(response, "\"limitYMax\":false"))
    {
        return 20;
    }
    if (!contains(response, "\"probeTriggered\":true"))
    {
        return 21;
    }
    if (!contains(response, "\"pose\":{\"x\":11.0000"))
    {
        return 22;
    }

    const std::string error = simplant::formatPlantErrorResponse("Unknown plant command");
    if (!contains(error, "\"ok\":false") || !contains(error, "Unknown plant command"))
    {
        return 23;
    }
    return 0;
}

int main()
{
    const int results[] = {
        testParseUnknown(), testSetLimits(), testTriggerProbe(), testSetEStop(), testResponseFormat(),
    };

    for (const int result : results)
    {
        if (result != 0)
        {
            return result;
        }
    }
    return 0;
}
