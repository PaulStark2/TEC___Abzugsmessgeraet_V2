// Unit tests for the M15 virtual outside world (SimPlant).
// The plant produces sensor signals only; every reaction is decided by Core.
#include "SimPlant.hpp"

#include "Machine.hpp"
#include "Types.hpp"

#include <cmath>

using simplant::PlantLimits;
using simplant::SimPlant;

static bool nearEqual(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

// Force stays zero in M15 regardless of pose.
static int testForceIsZero()
{
    SimPlant plant;
    const HostInputs inputs = plant.sample(12.5f, -7.0f, 3.0f);
    if (!nearEqual(inputs.force.fx, 0.0f) || !nearEqual(inputs.force.fy, 0.0f) ||
        !nearEqual(inputs.force.fz, 0.0f))
    {
        return 1;
    }
    return 0;
}

// Max limit switches trip on position, per axis and independently.
static int testPositionBasedMaxLimits()
{
    SimPlant plant;
    PlantLimits limits{};
    limits.x_max = 10.0f;
    limits.y_max = 20.0f;
    limits.z_max = 5.0f;
    plant.setLimits(limits);

    const HostInputs below = plant.sample(9.99f, 19.0f, 4.0f);
    if (below.limitXMax || below.limitYMax || below.limitZMax)
    {
        return 2;
    }

    const HostInputs on_x = plant.sample(10.0f, 0.0f, 0.0f);
    if (!on_x.limitXMax || on_x.limitYMax || on_x.limitZMax)
    {
        return 3;
    }

    const HostInputs above_all = plant.sample(11.0f, 25.0f, 6.0f);
    if (!above_all.limitXMax || !above_all.limitYMax || !above_all.limitZMax)
    {
        return 4;
    }

    // Min limits are not simulated in M15.
    if (above_all.limitXMin || above_all.limitYMin || above_all.limitZMin)
    {
        return 5;
    }
    return 0;
}

// Probe delivers a short pulse and returns to LOW on its own.
static int testProbePulse()
{
    SimPlant plant;
    plant.setProbePulseSeconds(0.1f);

    if (plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 6;
    }

    plant.triggerProbe();
    if (!plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 7;
    }

    plant.advance(0.05f);
    if (!plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 8;
    }

    plant.advance(0.05f);
    if (plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 9;
    }

    // Pulse timer must not go negative and must be re-armable.
    plant.advance(1.0f);
    plant.triggerProbe();
    if (!plant.sample(0.0f, 0.0f, 0.0f).probeTriggered)
    {
        return 10;
    }
    return 0;
}

static int testEStopPassthrough()
{
    SimPlant plant;
    plant.setEStop(true);
    if (!plant.sample(0.0f, 0.0f, 0.0f).e_stop || !plant.eStop())
    {
        return 11;
    }
    plant.setEStop(false);
    if (plant.sample(0.0f, 0.0f, 0.0f).e_stop)
    {
        return 12;
    }
    return 0;
}

// The plant must not stop motion: Core safety logic reacts to the limit signal.
static int testCoreDecidesLimitReaction()
{
    Machine machine;
    SimPlant plant;
    PlantLimits limits{};
    limits.x_max = 5.0f;
    plant.setLimits(limits);

    machine.moveTo(20.0f, 0.0f, 0.0f);

    constexpr float dt = 0.05f;
    bool limit_seen = false;
    for (int i = 0; i < 400; ++i)
    {
        const HostInputs inputs = plant.sample(machine.x(), machine.y(), machine.z());
        limit_seen = limit_seen || inputs.limitXMax;
        machine.setHostInputs(inputs);
        machine.tick(dt);
        plant.advance(dt);

        if (machine.machineState() == MachineState::Fault)
        {
            break;
        }
    }

    if (!limit_seen)
    {
        return 13;
    }
    // Reaction comes from Core safety (latched fault), not from the plant.
    if (machine.machineState() != MachineState::Fault)
    {
        return 14;
    }
    // Pose stopped at/after the trip point, well below the commanded target.
    if (machine.x() < limits.x_max || machine.x() > 20.0f)
    {
        return 15;
    }
    return 0;
}

// Plant state read-back mirrors the sampled sensors.
static int testStateReadback()
{
    SimPlant plant;
    PlantLimits limits{};
    limits.x_max = 1.0f;
    limits.y_max = 2.0f;
    limits.z_max = 3.0f;
    plant.setLimits(limits);
    plant.triggerProbe();

    const simplant::PlantState state = plant.state(1.5f, 0.0f, 0.0f);
    if (!state.limit_x_max || state.limit_y_max || state.limit_z_max)
    {
        return 16;
    }
    if (!state.probe_triggered || state.e_stop)
    {
        return 17;
    }
    if (!nearEqual(state.limits.y_max, 2.0f))
    {
        return 18;
    }
    return 0;
}

int main()
{
    const int results[] = {
        testForceIsZero(),
        testPositionBasedMaxLimits(),
        testProbePulse(),
        testEStopPassthrough(),
        testCoreDecidesLimitReaction(),
        testStateReadback(),
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
