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

static int testSetProbeDisc()
{
    const PlantCommand command = simplant::parsePlantCommand(
        "{\"cmd\":\"setProbeDisc\",\"x\":12.5,\"y\":-1,\"diameter\":9}");
    if (command.type != PlantCommandType::SetProbeDisc)
    {
        return 24;
    }
    if (!command.has_probe_x || !command.has_probe_y || command.has_probe_z || !command.has_probe_diameter)
    {
        return 25;
    }

    SimPlant plant;
    const float previous_z = plant.probeDisc().center.z;
    const float previous_thickness = plant.probeDisc().thickness;
    const float previous_diameter = plant.probeDisc().diameter;
    if (!simplant::applyPlantCommand(plant, command))
    {
        return 26;
    }
    if (!nearEqual(plant.probeDisc().center.x, 12.5f) || !nearEqual(plant.probeDisc().center.y, -1.0f))
    {
        return 27;
    }
    // Physical sizes are MachineConfig-owned; plant command must not overwrite them.
    if (!nearEqual(plant.probeDisc().diameter, previous_diameter))
    {
        return 28;
    }
    if (!nearEqual(plant.probeDisc().center.z, previous_z) ||
        !nearEqual(plant.probeDisc().thickness, previous_thickness))
    {
        return 29;
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
    if (!contains(response, "\"fixtureOpen\":false"))
    {
        return 40;
    }
    if (!contains(response, "\"diameter\":8.0000") || !contains(response, "\"toolDiameter\":2.5000") ||
        !contains(response, "\"toolCylindricalHeight\":15.0000"))
    {
        return 30;
    }
    if (!contains(response, "\"pose\":{\"x\":11.0000"))
    {
        return 22;
    }
    if (!contains(response, "\"referenceOffset\":{\"x\":0.0000"))
    {
        return 36;
    }

    const std::string withOffset = simplant::formatPlantStateResponse(
        plant.state(11.0f, 1.0f, 2.0f), 11.0f, 1.0f, 2.0f, 100.0f, 100.0f, 50.0f);
    if (!contains(withOffset, "\"referenceOffset\":{\"x\":100.0000") ||
        !contains(withOffset, "\"y\":100.0000") || !contains(withOffset, "\"z\":50.0000"))
    {
        return 37;
    }

    const std::string error = simplant::formatPlantErrorResponse("Unknown plant command");
    if (!contains(error, "\"ok\":false") || !contains(error, "Unknown plant command"))
    {
        return 23;
    }
    return 0;
}

static int testSetForceStiffness()
{
    SimPlant plant;
    if (!nearEqual(plant.forceStiffnessNPerMm(), 5.0f))
    {
        return 30;
    }
    const PlantCommand cmd =
        simplant::parsePlantCommand("{\"cmd\":\"setForceStiffness\",\"nPerMm\":2.5}");
    if (cmd.type != PlantCommandType::SetForceStiffness || !cmd.has_force_stiffness)
    {
        return 31;
    }
    if (!simplant::applyPlantCommand(plant, cmd))
    {
        return 32;
    }
    if (!nearEqual(plant.forceStiffnessNPerMm(), 2.5f))
    {
        return 33;
    }
    const auto state = plant.state(0.0f, 0.0f, 0.0f);
    if (!nearEqual(state.force_stiffness_n_per_mm, 2.5f))
    {
        return 34;
    }
    const std::string json = simplant::formatPlantStateResponse(state, 0.0f, 0.0f, 0.0f);
    if (!contains(json, "forceStiffnessNPerMm"))
    {
        return 35;
    }
    return 0;
}

static int testSetInjectedForce()
{
    SimPlant plant;
    const PlantCommand cmd =
        simplant::parsePlantCommand("{\"cmd\":\"setInjectedForce\",\"fx\":25,\"fy\":0}");
    if (cmd.type != PlantCommandType::SetInjectedForce || !cmd.has_injected_fx || cmd.has_injected_fz)
    {
        return 50;
    }
    if (!simplant::applyPlantCommand(plant, cmd))
    {
        return 51;
    }
    if (!nearEqual(plant.injectedForce().fx, 25.0f) || !nearEqual(plant.injectedForce().fy, 0.0f) ||
        !nearEqual(plant.injectedForce().fz, 0.0f))
    {
        return 52;
    }

    const auto state = plant.state(0.0f, 0.0f, 0.0f);
    if (!nearEqual(state.injected_fx, 25.0f))
    {
        return 53;
    }
    const std::string json = simplant::formatPlantStateResponse(state, 0.0f, 0.0f, 0.0f);
    if (!contains(json, "\"injectedFx\":25.0000") || !contains(json, "\"injectedFy\":0.0000"))
    {
        return 54;
    }

    if (simplant::applyPlantCommand(
            plant, simplant::parsePlantCommand("{\"cmd\":\"setInjectedForce\",\"fx\":201}")))
    {
        return 55;
    }
    if (!nearEqual(plant.injectedForce().fx, 25.0f))
    {
        return 56;
    }

    if (!simplant::applyPlantCommand(plant, simplant::parsePlantCommand("{\"cmd\":\"setInjectedForce\"}")))
    {
        return 57;
    }
    if (!nearEqual(plant.injectedForce().fx, 0.0f))
    {
        return 58;
    }
    return 0;
}

static int testFixtureOpenInStateJson()
{
    SimPlant plant;
    plant.setFixtureValve(true);
    const auto state = plant.state(0.0f, 0.0f, 0.0f);
    if (!state.fixture_open)
    {
        return 60;
    }
    const std::string json = simplant::formatPlantStateResponse(state, 0.0f, 0.0f, 0.0f);
    if (!contains(json, "\"fixtureOpen\":true"))
    {
        return 61;
    }
    plant.setFixtureValve(false);
    const std::string closed = simplant::formatPlantStateResponse(plant.state(0.0f, 0.0f, 0.0f),
                                                                 0.0f, 0.0f, 0.0f);
    if (!contains(closed, "\"fixtureOpen\":false"))
    {
        return 62;
    }
    return 0;
}

int main()
{
    const int results[] = {
        testParseUnknown(), testSetLimits(), testTriggerProbe(), testSetEStop(), testSetProbeDisc(),
        testSetForceStiffness(), testSetInjectedForce(), testResponseFormat(), testFixtureOpenInStateJson(),
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
