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

// Core-independent virtual probe disc for architecture tests.
// disc_center represents the SimPlant "ground truth" disc center in machine coordinates.
inline HostInputs withVirtualProbeDisc(const Machine& machine, const TestPoint& disc_center,
                                         HostInputs in = HostInputs{})
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    if (toolContactsProbeDisc(machine.x(), machine.y(), machine.z(), disc_center.x, disc_center.y, disc_center.z,
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

// Default helper: disc center equals the configured probe_offset_reference.
// This keeps legacy unit tests stable; architecture tests should prefer withVirtualProbeDisc().
inline HostInputs withVirtualProbe(const Machine& machine, HostInputs in = HostInputs{})
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    return withVirtualProbeDisc(machine, probe.probe_offset_reference, in);
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

// For tests that require the automatic procedure to start,
// we must calibrate probe_center via a standalone M19 run first.
inline bool calibrateProbeCenter(Machine& machine, int max_ticks = 2500)
{
    // Preserve the test's intended machine pose. Some unit tests assume that
    // after homing the TCP is at/near (0,0,...) when they start an automatic process.
    const TestPoint start_pose{machine.x(), machine.y(), machine.z()};

    // Manual calibration is defined as: user jogs TCP near the probe,
    // then M19 uses current TCP pose as search_reference.
    // For the default virtual probe, the disc center equals probe_offset_reference.
    const MachineConfig::Probe& probe_cfg = machine.machineConfig().probe;
    machine.moveTo(probe_cfg.probe_offset_reference.x, probe_cfg.probe_offset_reference.y,
                    probe_cfg.probe_offset_reference.z);
    for (int i = 0; i < 600 && machine.motionState() != MotionState::AtTarget; ++i)
    {
        machine.setHostInputs(withFreshForce(HostInputs{}));
        machine.tick(0.05f);
    }

    TestExecutor ex(machine);
    if (!ex.startProbeCalibration())
    {
        return false;
    }
    for (int i = 0; i < max_ticks && ex.processState() == ProcessState::Running; ++i)
    {
        machine.setHostInputs(withPlantInputs(machine));
        ex.tick(0.05f);
    }
    const bool ok = machine.calibrationSet().probe_center_valid;

    // Restore pose for tests. Automatic procedures assume the machine starts at Safe-Z,
    // and `atSafeZ()` is intentionally strict.
    const float safe_z = machine.machineConfig().motion.safe_z;
    machine.moveTo(start_pose.x, start_pose.y, safe_z);
    for (int i = 0; i < 600 && machine.motionState() != MotionState::AtTarget; ++i)
    {
        machine.setHostInputs(withFreshForce(HostInputs{}));
        machine.tick(0.05f);
    }

    return ok;
}