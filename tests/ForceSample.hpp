#pragma once

#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "ProbeGeometry.hpp"
#include "TestExecutor.hpp"

#include <cstdint>

// Stamp a new force conversion so Machine freshness tracking resets.
inline HostInputs withFreshForce(HostInputs in)
{
    static std::uint32_t seq = 1;
    in.forceSampleValid = true;
    in.forceSampleSequence = seq++;
    in.hostHeartbeatValid = true;
    in.hostHeartbeatSequence = in.forceSampleSequence;
    return in;
}

// Geometric probe disc at the configured nominal center (aligned plant for tests).
inline HostInputs withVirtualProbe(const Machine& machine, HostInputs in = HostInputs{})
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    const TestPoint disc = probe.nominal_probe;
    if (toolContactsProbeDisc(machine.x(), machine.y(), machine.z(), disc.x, disc.y, disc.z,
                              probe.measuring_head_diameter, probe.measuring_head_thickness,
                              probe.tool_diameter, probe.tool_cylindrical_height))
    {
        static std::uint32_t probe_seq = 1u;
        in.probeTriggered = true;
        in.probeTriggerPoseValid = true;
        in.probeTriggerX = machine.x();
        in.probeTriggerY = machine.y();
        in.probeTriggerZ = machine.z();
        // Mirror STM32 behavior: sequence number advances per trigger event.
        in.probeTriggerSequence = probe_seq++;
    }
    return in;
}

inline HostInputs withPlantInputs(const Machine& machine, HostInputs in = HostInputs{})
{
    return withFreshForce(withVirtualProbe(machine, in));
}

// Deliver a fresh sample then tick. Use in tests that previously ticked Machine
// without a host force conversion.
inline void tickWithForce(Machine& machine, float dt, HostInputs in = HostInputs{})
{
    machine.setHostInputs(withPlantInputs(machine, in));
    machine.tick(dt);
}

inline bool phaseNeedsProcessForce(ProcedurePhase phase)
{
    return phase == ProcedurePhase::InsertInitial || phase == ProcedurePhase::OptimizeInsert ||
           phase == ProcedurePhase::TestOrAge;
}

// Tensile-positive Fz at the configured target during insert/optimize/measure so complete-run
// tests do not treat AtTarget as Phase-1 success.
inline HostInputs withProcessForce(const TestExecutor& ex, const Machine& machine, HostInputs in = HostInputs{})
{
    if (phaseNeedsProcessForce(ex.procedurePhase()))
    {
        in.force.fz = machine.testConfiguration().target_force_N;
    }
    return withPlantInputs(machine, in);
}

// Drive the Core homing sequence Z+ → X+ → Y+ to a Homed state for automatic-start tests.
inline bool homeMachine(Machine& machine)
{
    MachineConfig cfg = machine.machineConfig();
    cfg.probe.num_fine_searches = 0;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(cfg, err))
    {
        return false;
    }

    HostInputs in = withFreshForce(HostInputs{});
    machine.setHostInputs(in);
    if (!machine.home())
    {
        return false;
    }
    in.limitZMax = true;
    machine.setHostInputs(withFreshForce(in));
    machine.tick(0.05f);
    in.limitZMax = false;
    in.limitXMax = true;
    machine.setHostInputs(withFreshForce(in));
    machine.tick(0.05f);
    in.limitXMax = false;
    in.limitYMax = true;
    machine.setHostInputs(withFreshForce(in));
    machine.tick(0.05f);
    return machine.isHomed();
}