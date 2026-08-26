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
    // Soft +max switches for end-of-procedure Machine::home() in unit tests
    // (SimProtocolBridge gets these from SimPlant; bare Machine tests do not).
    if (machine.machineState() == MachineState::Homing)
    {
        constexpr float kXMax = 100.0f;
        constexpr float kYMax = 100.0f;
        constexpr float kZMax = 50.0f;
        in.limitXMax = machine.x() >= kXMax;
        in.limitYMax = machine.y() >= kYMax;
        in.limitZMax = machine.z() >= kZMax;
    }
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

// Drive Z+ → X+ → Y+ to Homed. First call on a fresh machine defines MCS;
// subsequent calls use reference home() only.
// If DefineMcs runs, plate/probe taught before this helper are restored afterward so
// existing unit tests that setPlateOrigin() then homeMachine() keep working.
// Production GUI never depends on that restore (it teaches after Define).
inline bool homeMachine(Machine& machine)
{
    MachineConfig cfg = machine.machineConfig();
    cfg.probe.num_fine_searches = 0;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(cfg, err))
    {
        return false;
    }

    const bool need_define = !machine.machineCoordinateSystemValid();
    const TestPoint plate_before = machine.calibrationSet().plate_origin;
    const bool plate_v_before = machine.calibrationSet().plate_origin_valid;
    const TestPoint probe_before = machine.calibrationSet().probe_center;
    const bool probe_v_before = machine.calibrationSet().probe_center_valid;
    const TestPoint tool_before = machine.calibrationSet().tool_offset;

    HostInputs in = withFreshForce(HostInputs{});
    machine.setHostInputs(in);
    const bool started =
        need_define ? machine.defineMachineCoordinateSystem() : machine.home();
    if (!started)
    {
        return false;
    }

    auto advanceAxis = [&](HomingState seek_phase, bool& limit_flag) {
        if (!need_define)
        {
            // Reference harness: trip immediately so raw barely moves. A long seek
            // before the soft switch would inflate reference_offset and desync
            // SimPlant raw discs from machine-frame probe_center.
            limit_flag = true;
            machine.setHostInputs(withFreshForce(in));
            machine.tick(0.05f);
            limit_flag = false;
            for (int i = 0; i < 400 && machine.homingState() == seek_phase; ++i)
            {
                machine.setHostInputs(withFreshForce(in));
                machine.tick(0.05f);
            }
            return;
        }
        // DefineMcs: seek briefly so the limit is not asserted at the start pose.
        for (int i = 0; i < 80 && machine.homingState() == seek_phase; ++i)
        {
            limit_flag = false;
            machine.setHostInputs(withFreshForce(in));
            machine.tick(0.05f);
        }
        limit_flag = true;
        machine.setHostInputs(withFreshForce(in));
        machine.tick(0.05f);
        limit_flag = false;
        for (int i = 0; i < 400 && machine.homingState() == seek_phase; ++i)
        {
            machine.setHostInputs(withFreshForce(in));
            machine.tick(0.05f);
        }
    };

    advanceAxis(HomingState::HomingZ, in.limitZMax);
    if (machine.homingState() != HomingState::HomingX && machine.homingState() != HomingState::Homed)
    {
        return false;
    }
    advanceAxis(HomingState::HomingX, in.limitXMax);
    if (machine.homingState() != HomingState::HomingY && machine.homingState() != HomingState::Homed)
    {
        return false;
    }
    advanceAxis(HomingState::HomingY, in.limitYMax);
    if (!machine.isHomed())
    {
        return false;
    }

    if (need_define)
    {
        if (plate_v_before)
        {
            machine.setPlateOrigin(plate_before);
        }
        if (probe_v_before)
        {
            machine.setProbeCenter(probe_before);
            machine.setToolOffset(tool_before);
        }
    }
    return true;
}

// For tests that require the automatic procedure to start,
// we must calibrate probe_center via a standalone M19 run first.
inline bool calibrateProbeCenter(Machine& machine, int max_ticks = 12000)
{
    // Preserve the test's intended machine pose. Some unit tests assume that
    // after homing the TCP is at/near (0,0,...) when they start an automatic process.
    const TestPoint start_pose{machine.x(), machine.y(), machine.z()};

    // Manual calibration: operator jogs TCP to the hub above the disc.
    const MachineConfig::Probe& probe_cfg = machine.machineConfig().probe;
    machine.moveTo(probe_cfg.probe_offset_reference.x, probe_cfg.probe_offset_reference.y,
                    probe_cfg.probe_offset_reference.z + probe_cfg.approach_distance);
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

    // Ensure config lock from the standalone process is released even if the
    // tick budget ended before ProcessState::Completed (slower fine-speed M19).
    machine.setRuntimeConfigLocked(false);
    machine.setCommunicationWatchdogEnabled(false);

    // Standalone M19 completion calls Machine::stop() → Stopped. moveTo recovers.
    machine.moveTo(start_pose.x, start_pose.y, start_pose.z);
    for (int i = 0; i < 600 && machine.motionState() != MotionState::AtTarget; ++i)
    {
        machine.setHostInputs(withFreshForce(HostInputs{}));
        machine.tick(0.05f);
    }

    if (!ok)
    {
        return false;
    }

    // Prefer latching isHomed at the MCS home pose without a second soft-limit
    // Reference seek (which can disturb offsets vs SimPlant). Fall back to home().
    if (machine.confirmHomedIfAtHome(1.0f))
    {
        return true;
    }
    return homeMachine(machine);
}