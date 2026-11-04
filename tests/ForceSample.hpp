#pragma once

#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "TestExecutor.hpp"

#include <cmath>
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

inline HostInputs withPlantInputs(const Machine& machine, HostInputs in = HostInputs{})
{
    // Soft +max switches for end-of-procedure Machine::home() in unit tests
    // (SimProtocolBridge gets these from SimPlant; bare Machine tests do not).
    if (machine.machineState() == MachineState::Homing)
    {
        // Match Machine homing sense: X at Min, Y/Z at +Max (see homeMachine()).
        constexpr float kXMin = 0.0f;
        constexpr float kYMax = 100.0f;
        constexpr float kZMax = 50.0f;
        in.limitXMin = machine.rawX() <= kXMin + 0.5f;
        in.limitYMax = machine.rawY() >= kYMax;
        in.limitZMax = machine.rawZ() >= kZMax;
    }
    return withFreshForce(in);
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
    // Unclamp AfterTest re-seats to target force before opening.
    return phase == ProcedurePhase::InsertInitial || phase == ProcedurePhase::ProbeRemeasure ||
           phase == ProcedurePhase::TestOrAge || phase == ProcedurePhase::Unclamp;
}

// Radial bore around a latched center. Used for PreClamp and ProbeCalibrate (z_first).
inline void applySyntheticBoreForce(HostInputs& in, const Machine& machine, float cx, float cy,
                                    float cz, float radius_mm, float wall_N, float floor_N)
{
    if (in.force.fx != 0.0f || in.force.fy != 0.0f)
    {
        return;
    }
    const float dx = machine.x() - cx;
    const float dy = machine.y() - cy;
    const float radius = std::hypot(dx, dy);
    if (radius > radius_mm)
    {
        const float penetration = radius - radius_mm;
        constexpr float kStiffness = 80.0f;
        in.force.fx = kStiffness * penetration * dx / radius;
        in.force.fy = kStiffness * penetration * dy / radius;
        if (std::fabs(in.force.fx) < wall_N && std::fabs(in.force.fy) < wall_N)
        {
            in.force.fx = (dx >= 0.0f ? 1.0f : -1.0f) * (wall_N + 0.2f);
            in.force.fy = (dy >= 0.0f ? 1.0f : -1.0f) * (wall_N + 0.2f);
        }
    }
    if (machine.z() < cz - 0.05f && in.force.fz == 0.0f)
    {
        in.force.fz = floor_N + 0.5f;
    }
}

// Radial bore around the pose where PreClamp / ProbeCalibrate is first entered.
// Re-latch on every transition into a bore phase (pointer identity is unsafe across tests).
inline HostInputs withProcedurePlant(const TestExecutor& ex, const Machine& machine, HostInputs in = HostInputs{})
{
    struct Latch
    {
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        bool active = false;
    };
    static Latch latch;
    static bool was_bore_phase = false;
    const ProcedurePhase ph = ex.procedurePhase();
    const bool bore_phase = ph == ProcedurePhase::PreClampXyCorrect ||
                            ph == ProcedurePhase::ProbeCalibrate ||
                            ph == ProcedurePhase::ProbeRemeasure;
    if (!bore_phase)
    {
        was_bore_phase = false;
        latch.active = false;
    }
    else
    {
        const bool offset_only =
            (ph == ProcedurePhase::ProbeCalibrate || ph == ProcedurePhase::ProbeRemeasure) &&
            machine.calibrationSet().probe_center_valid;
        const float dist_xy =
            latch.active ? std::hypot(machine.x() - latch.cx, machine.y() - latch.cy) : 1e9f;
        // Re-latch on bore-phase entry, when TCP jumped (stack Machine reuse across tests),
        // or when OffsetOnly must follow the stored Install center.
        if (!was_bore_phase || !latch.active || dist_xy > 2.0f || offset_only)
        {
            was_bore_phase = true;
            latch.active = true;
            if (offset_only)
            {
                const TestPoint& c = machine.calibrationSet().probe_center;
                latch.cx = c.x;
                latch.cy = c.y;
                latch.cz = c.z;
            }
            else
            {
                latch.cx = machine.x();
                latch.cy = machine.y();
                latch.cz = machine.z() - 3.0f; // floor a bit below start height
            }
        }
        // Skip Safe-Z / long approaches: only engage near the bore work height.
        const float standoff = machine.machineConfig().probe.approach_distance;
        const bool near_work_z = machine.z() <= latch.cz + standoff + 1.0f;
        if (near_work_z)
        {
            const float wall = (ph == ProcedurePhase::ProbeCalibrate ||
                                ph == ProcedurePhase::ProbeRemeasure)
                                   ? machine.machineConfig().probe.wall_force_N
                                   : machine.machineConfig().force.xy_tolerance_N;
            const float floor = machine.machineConfig().probe.floor_force_N;
            applySyntheticBoreForce(in, machine, latch.cx, latch.cy, latch.cz, 0.4f, wall, floor);
        }
    }
    return withPlantInputs(machine, in);
}

inline HostInputs withProcessForce(const TestExecutor& ex, const Machine& machine, HostInputs in = HostInputs{})
{
    const ProcedurePhase ph = ex.procedurePhase();
    const float target = machine.testConfiguration().target_force_N + 0.05f;
    if (ph == ProcedurePhase::InsertInitial || ph == ProcedurePhase::Unclamp)
    {
        in.force.fz = target;
    }
    else if (ph == ProcedurePhase::TestOrAge)
    {
        // Leg 1 insert → seat force. Legs 0/2 extract: Druck → Zug → kraftfrei
        // (must see tensile before accepting 0 N as Ausgliederpunkt).
        if (ex.procedure().cycleLeg() == 1)
        {
            in.force.fz = target;
        }
        else
        {
            const float dz = machine.z() - ex.procedure().insertionZ();
            if (dz < 0.3f)
            {
                in.force.fz = target;
            }
            else if (dz < 1.0f)
            {
                in.force.fz = -target;
            }
            else
            {
                in.force.fz = 0.0f;
            }
        }
    }
    else if (ph == ProcedurePhase::ProbeRemeasure)
    {
        in.force.fz = 0.0f;
    }
    return withProcedurePlant(ex, machine, in);
}

// Drive Z+ → X+ → Y+ to Homed. First call on a fresh machine defines MCS;
// subsequent calls use reference home() only.
inline bool homeMachine(Machine& machine)
{
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
        float trip = 0.0f;
        switch (seek_phase)
        {
        case HomingState::HomingZ:
            trip = machine.rawZ() + 1.0f;
            break;
        case HomingState::HomingX:
            trip = machine.rawX() - 1.0f;
            break;
        case HomingState::HomingY:
            trip = machine.rawY() + 1.0f;
            break;
        default:
            break;
        }
        for (int i = 0; i < 4000 && machine.homingState() == seek_phase; ++i)
        {
            switch (seek_phase)
            {
            case HomingState::HomingZ:
                limit_flag = machine.rawZ() >= trip;
                break;
            case HomingState::HomingX:
                limit_flag = machine.rawX() <= trip;
                break;
            case HomingState::HomingY:
                limit_flag = machine.rawY() >= trip;
                break;
            default:
                limit_flag = false;
                break;
            }
            machine.setHostInputs(withFreshForce(in));
            machine.tick(0.05f);
            if (machine.homingState() == HomingState::HomingError)
            {
                return;
            }
        }
    };

    advanceAxis(HomingState::HomingZ, in.limitZMax);
    if (machine.homingState() != HomingState::HomingX && machine.homingState() != HomingState::Homed)
    {
        return false;
    }
    advanceAxis(HomingState::HomingX, in.limitXMin);
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

// For tests that require automatic procedure start: Install probe_center via bore calib.
inline bool calibrateProbeCenter(Machine& machine, int max_ticks = 12000)
{
    const TestPoint start_pose{machine.x(), machine.y(), machine.z()};
    constexpr TestPoint kBore{40.0f, 40.0f, 5.0f};

    machine.moveTo(kBore.x, kBore.y, kBore.z + machine.machineConfig().probe.approach_distance);
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
        machine.setHostInputs(withProcedurePlant(ex, machine));
        ex.tick(0.05f);
    }
    const bool ok = machine.calibrationSet().probe_center_valid;

    machine.setRuntimeConfigLocked(false);
    machine.setCommunicationWatchdogEnabled(false);

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

    if (machine.confirmHomedIfAtHome(1.0f))
    {
        return true;
    }
    return homeMachine(machine);
}
