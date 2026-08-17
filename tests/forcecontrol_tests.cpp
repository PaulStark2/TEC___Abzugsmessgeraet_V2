// Deterministic unit tests for M10 force control.
#include "Machine.hpp"
#include "TestExecutor.hpp"

#include "HostInputs.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

static void setForce(Machine& machine, float fx, float fy, float fz)
{
    HostInputs in{};
    in.force = ForceVector{fx, fy, fz};
    in.e_stop = false;
    machine.setHostInputs(in);
}

static void runUntilNotRunning(TestExecutor& ex, int max_steps, float dt, Machine& machine,
                                 float fx, float fy, float fz_after_each_tick,
                                 bool force_constant = true)
{
    // Helper: drives one deterministic trace for Fault/Stop/Completed checks.
    // When force_constant is false, fz_after_each_tick is interpreted as a value to use only
    // at the last tick (remaining ticks use fz=0).
    for (int i = 0; i < max_steps && ex.processState() == ProcessState::Running; ++i)
    {
        if (force_constant)
        {
            setForce(machine, fx, fy, fz_after_each_tick);
        }
        else
        {
            const float fz = (i == max_steps - 1) ? fz_after_each_tick : 0.0f;
            setForce(machine, fx, fy, fz);
        }
        ex.tick(dt);
    }
}

int main()
{
    // Common dt for stable deterministic behavior.
    constexpr float kDt = 0.02f;

    // -------------------------------
    // A) XY: ignore before SlowZApproach
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.slow_z_distance_threshold_units = 0.0001f; // will be Normal for the first few ticks
        params.fz_safety_threshold_N = 1000000.0f; // disabled

        Machine m(params);
        m.moveTo(0.0f, 0.0f, 2.0f); // X/Y at target => no motion unless XY correction updates targets

        for (int i = 0; i < 3; ++i)
        {
            // High Fx/Fy that would trigger correction if SlowZApproach were active.
            setForce(m, +12.0f, -12.0f, 0.0f);
            m.tick(kDt);
        }

        if (!nearEqual(m.x(), 0.0f) || !nearEqual(m.y(), 0.0f))
        {
            return 1; // XY must remain unchanged in NormalZApproach
        }
    }

    // -------------------------------
    // A) XY: continuous hysteresis (Fx only) and restart
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.slow_z_distance_threshold_units = 1000.0f; // always SlowZApproach => XY active immediately
        params.fz_safety_threshold_N = 1000000.0f; // disabled

        Machine m(params);
        m.moveTo(0.0f, 0.0f, 2.0f);

        const float x0 = m.x();

        // ON phase: absFx >= on => correction active.
        setForce(m, +12.0f, 0.0f, 0.0f);
        m.tick(kDt);
        const float x1 = m.x();
        if (!(x1 < x0))
        {
            return 2; // Fx>0 => X should move negative
        }

        m.tick(kDt);
        const float x2 = m.x();
        if (!(x2 < x1))
        {
            return 3; // continuous correction while still ON
        }

        // Within band: on > absFx > off => hysteresis remains active.
        setForce(m, +7.0f, 0.0f, 0.0f);
        m.tick(kDt);
        const float x3 = m.x();
        if (!(x3 < x2))
        {
            return 4; // correction must continue within ON/OFF band
        }

        // OFF: absFx <= off => correction stops.
        const float x_before_off = m.x();
        setForce(m, +5.0f, 0.0f, 0.0f);
        m.tick(kDt); // deactivation tick must not change X target further
        const float x_after_off_tick = m.x();
        if (!nearEqual(x_after_off_tick, x_before_off))
        {
            return 5; // X must stop changing once absFx <= OFF
        }

        // Still OFF: additional ticks must not modify X anymore.
        m.tick(kDt);
        const float x_after_off_2 = m.x();
        if (!nearEqual(x_after_off_2, x_after_off_tick))
        {
            return 6; // correction remains off with absFx <= OFF
        }

        // Restart: absFx >= on again => correction re-activates.
        setForce(m, +12.0f, 0.0f, 0.0f);
        m.tick(kDt);
        const float x_restart = m.x();
        if (!(x_restart < x_after_off_2))
        {
            return 7; // must move again after exceeding ON threshold
        }

        // Independence: Fy=0 must not affect X/Y behavior; Y must remain at 0.
        if (!nearEqual(m.y(), 0.0f))
        {
            return 8; // Y must remain unchanged when Fy is within OFF band
        }
    }

    // -------------------------------
    // A) XY independence: X controlled by Fx, Y controlled by Fy
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.slow_z_distance_threshold_units = 1000.0f;
        params.fz_safety_threshold_N = 1000000.0f;

        Machine m(params);
        m.moveTo(0.0f, 0.0f, 2.0f);

        // Stage 1: only Fx active.
        setForce(m, +12.0f, 0.0f, 0.0f);
        m.tick(kDt);
        const float x1 = m.x();
        const float y1 = m.y();
        if (!(x1 < 0.0f) || !nearEqual(y1, 0.0f))
        {
            return 9; // X should move, Y must stay 0
        }

        m.tick(kDt);
        const float x2 = m.x();
        const float y2 = m.y();
        if (!(x2 < x1) || !nearEqual(y2, 0.0f))
        {
            return 10; // continued X correction, Y unaffected
        }

        // Stage 2: only Fy active (Fx drops to 0 => X corrections stop).
        const float x_before_fy = m.x();
        setForce(m, 0.0f, +12.0f, 0.0f);
        m.tick(kDt);
        const float x_after_fx_off = m.x();
        const float y_after_fy_on = m.y();
        if (!nearEqual(x_after_fx_off, x_before_fy))
        {
            return 11; // X must stop changing when Fx is within OFF band
        }
        if (!(y_after_fy_on < 0.0f))
        {
            return 12; // Fy>0 => Y should move negative
        }
    }

    // -------------------------------
    // B) Z: abs(Fz) safety triggers Fault (positive and negative)
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.slow_z_distance_threshold_units = 1000.0f; // would activate XY immediately if safety did not take priority
        params.fz_safety_threshold_N = 10.0f;

        // Positive Fz case.
        {
            Machine m(params);
            m.moveTo(0.0f, 0.0f, 2.0f);

            const float x0 = m.x();
            const float y0 = m.y();
            const float z0 = m.z();

            setForce(m, +12.0f, +12.0f, +11.0f); // safety must trigger
            m.tick(kDt);

            if (m.machineState() != MachineState::Fault)
            {
                return 13; // must enter Fault
            }
            if (!nearEqual(m.x(), x0) || !nearEqual(m.y(), y0) || !nearEqual(m.z(), z0))
            {
                return 14; // position must be frozen deterministically on safety tick
            }
            if (m.motionState() == MotionState::AtTarget)
            {
                return 15; // stopMotion must not create AtTarget completion
            }
        }

        // Negative Fz case.
        {
            Machine m(params);
            m.moveTo(0.0f, 0.0f, 2.0f);

            const float x0 = m.x();
            const float y0 = m.y();
            const float z0 = m.z();

            setForce(m, -12.0f, +12.0f, -11.0f); // safety must trigger with abs(Fz)
            m.tick(kDt);

            if (m.machineState() != MachineState::Fault)
            {
                return 16;
            }
            if (!nearEqual(m.x(), x0) || !nearEqual(m.y(), y0) || !nearEqual(m.z(), z0))
            {
                return 17;
            }
            if (m.motionState() == MotionState::AtTarget)
            {
                return 18;
            }
        }
    }

    // -------------------------------
    // C) Priority: safety blocks XY in the same tick
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.slow_z_distance_threshold_units = 1000.0f; // would enable XY
        params.fz_safety_threshold_N = 10.0f;

        Machine m(params);
        m.moveTo(0.0f, 0.0f, 2.0f);

        const float x0 = m.x();
        const float y0 = m.y();

        setForce(m, +12.0f, +12.0f, +11.0f); // safety tick
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 19;
        }
        // XY must not have executed in the safety tick.
        if (!nearEqual(m.x(), x0) || !nearEqual(m.y(), y0))
        {
            return 20;
        }
    }

    // -------------------------------
    // C) Fault semantics in TestExecutor: Fault => Aborted (not Completed)
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.slow_z_distance_threshold_units = 1000.0f;
        params.fz_safety_threshold_N = 10.0f;

        Machine machine(params);
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        TestExecutor ex(machine);

        TestProgram program;
        if (!program.add(TestPoint{0.0f, 0.0f, 2.0f}))
        {
            return 21;
        }
        ex.setProgram(program);
        if (!ex.start())
        {
            return 22;
        }

        // First tick: no safety fault.
        setForce(machine, 0.0f, 0.0f, 0.0f);
        ex.tick(kDt);
        if (ex.processState() != ProcessState::Running)
        {
            return 23; // must still run
        }

        // Second tick: trigger safety => MachineState::Fault => Aborted.
        setForce(machine, +12.0f, +12.0f, +11.0f);
        ex.tick(kDt);

        if (ex.processState() != ProcessState::Aborted)
        {
            return 24;
        }
        if (machine.machineState() != MachineState::Fault)
        {
            return 25;
        }
    }

    // -------------------------------
    // D) Max XY correction clamp (independent X/Y, per direction)
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fx_threshold_on_N = 10.0f;
        params.fx_threshold_off_N = 5.0f;
        params.fy_threshold_on_N = 10.0f;
        params.fy_threshold_off_N = 5.0f;
        params.dx_step_units = 0.02f;
        params.dy_step_units = 0.02f;
        params.max_x_correction_units = 0.06f;
        params.max_y_correction_units = 0.04f;
        params.slow_z_distance_threshold_units = 1000.0f; // SlowZApproach from tick 1
        params.fz_safety_threshold_N = 1000000.0f;

        Machine m(params);
        m.moveTo(0.0f, 0.0f, 2.0f);

        // +Fx drives target in -X; clamp at commanded_x - max_x_correction.
        for (int i = 0; i < 50; ++i)
        {
            setForce(m, +12.0f, 0.0f, 0.0f);
            m.tick(kDt);
        }
        if (m.x() < -params.max_x_correction_units - 1e-3f)
        {
            return 26; // X must not exceed negative correction limit
        }

        const float x_at_limit = m.x();
        for (int i = 0; i < 30; ++i)
        {
            setForce(m, +12.0f, 0.0f, 0.0f);
            m.tick(kDt);
        }
        if (!nearEqual(m.x(), x_at_limit, 1e-3f))
        {
            return 27; // further +Fx must not shift X past the limit
        }

        // -Fy drives target in +Y; clamp at commanded_y + max_y_correction.
        for (int i = 0; i < 50; ++i)
        {
            setForce(m, 0.0f, -12.0f, 0.0f);
            m.tick(kDt);
        }
        if (m.y() > params.max_y_correction_units + 1e-3f)
        {
            return 28; // Y must not exceed positive correction limit
        }

        const float y_at_limit = m.y();
        for (int i = 0; i < 30; ++i)
        {
            setForce(m, 0.0f, -12.0f, 0.0f);
            m.tick(kDt);
        }
        if (!nearEqual(m.y(), y_at_limit, 1e-3f))
        {
            return 29; // further -Fy must not shift Y past the limit
        }
    }

    return 0;
}

