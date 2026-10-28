#include "ForceFilter.hpp"
#include "ForceSample.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"

#include <cmath>

static bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

static void setForce(Machine& machine, float fx, float fy, float fz)
{
    HostInputs in{};
    in.force = ForceVector{fx, fy, fz};
    machine.setHostInputs(withFreshForce(in));
}

static bool setTau(Machine& machine, float tau_s)
{
    MachineConfig cfg = machine.machineConfig();
    cfg.force.force_filter = tau_s;
    ConfigValidationError err = ConfigValidationError::Ok;
    return machine.trySetMachineConfig(cfg, err);
}

static int testPassthrough()
{
    ForceFilter filter;
    const ForceVector y = filter.update(ForceVector{9.0f, 8.0f, 7.0f}, 0.05f, 0.0f);
    if (!near(y.fx, 9.0f, 1e-6f) || !near(y.fy, 8.0f, 1e-6f) || !near(y.fz, 7.0f, 1e-6f))
    {
        return 1;
    }

    Machine machine;
    if (!setTau(machine, 0.0f))
    {
        return 2;
    }
    setForce(machine, 3.25f, -1.5f, 0.5f);
    machine.tick(0.001f);
    const ForceVector published = machine.hostInputs().force;
    if (!near(published.fx, 3.25f, 1e-5f) || !near(published.fy, -1.5f, 1e-5f) ||
        !near(published.fz, 0.5f, 1e-5f))
    {
        return 3;
    }
    return 0;
}

// y(t) = u * (1 - exp(-t / tau)) from rest, no overshoot.
static int testStepResponse()
{
    ForceFilter filter;
    filter.reset(ForceVector{0.0f, 0.0f, 0.0f});
    const float tau = 0.05f;
    const float dt = 0.001f;
    const ForceVector u{10.0f, -4.0f, 2.5f};
    float prev_fx = 0.0f;
    for (int n = 1; n <= 200; ++n)
    {
        const ForceVector y = filter.update(u, dt, tau);
        const double settled = 1.0 - std::exp(-(static_cast<double>(n) * dt) / tau);
        if (!near(y.fx, static_cast<float>(10.0 * settled), 2e-4f) ||
            !near(y.fy, static_cast<float>(-4.0 * settled), 2e-4f) ||
            !near(y.fz, static_cast<float>(2.5 * settled), 2e-4f))
        {
            return 10;
        }
        if (!(y.fx > prev_fx) || !(y.fx < u.fx))
        {
            return 11;
        }
        prev_fx = y.fx;
    }

    Machine machine;
    if (!setTau(machine, tau))
    {
        return 12;
    }
    setForce(machine, 0.0f, 0.0f, 0.0f);
    machine.tick(dt);
    setForce(machine, u.fx, u.fy, u.fz);
    machine.tick(tau); // dt = tau => factor 1 - exp(-1)
    const double factor = 1.0 - std::exp(-1.0);
    const ForceVector published = machine.hostInputs().force;
    if (!near(published.fx, static_cast<float>(u.fx * factor), 1e-3f) ||
        !near(published.fy, static_cast<float>(u.fy * factor), 1e-3f) ||
        !near(published.fz, static_cast<float>(u.fz * factor), 1e-3f))
    {
        return 13;
    }
    return 0;
}

// Hard limit must trip on the raw sample while the low-pass is still near zero.
static int testWatchdogUsesRaw()
{
    Machine machine;
    if (!setTau(machine, 0.05f))
    {
        return 20;
    }
    setForce(machine, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 5; ++i)
    {
        machine.tick(0.001f);
    }
    setForce(machine, 0.0f, 0.0f, 100.0f);
    machine.tick(0.001f);
    if (machine.machineState() != MachineState::Fault ||
        machine.lastFaultReason() != FaultReason::ForceHardLimit)
    {
        return 21;
    }
    return 0;
}

// Alternating 1.2 N / 0 N. The 50 ms decision lands on a 0 N sample.
// Without tau the stepper never sees F_on. With tau = 0.05 s the mean stays
// above 0.5 N and at least one step is issued.
static int runChatter(float tau, bool expect_step)
{
    Machine::ForceControlParams params{};
    params.fx_threshold_on_N = 0.5f;
    params.fx_threshold_off_N = 0.25f;
    params.fy_threshold_on_N = 0.5f;
    params.fy_threshold_off_N = 0.25f;
    params.dx_step_units = 0.05f;
    params.dy_step_units = 0.05f;
    params.fz_safety_threshold_N = 1000000.0f;

    Machine machine(params);
    if (!setTau(machine, tau))
    {
        return 1;
    }
    machine.moveTo(0.0f, 0.0f, 2.0f);
    setForce(machine, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 80 && machine.motionState() != MotionState::AtTarget; ++i)
    {
        machine.tick(0.05f);
    }
    machine.setPreClampXyEnabled(true);

    const float x0 = machine.x();
    for (int i = 0; i < 200; ++i)
    {
        const float fx = (i % 2 == 0) ? 1.2f : 0.0f;
        setForce(machine, fx, 0.0f, 0.0f);
        machine.tick(0.001f);
    }
    const bool stepped = machine.x() < x0 - 0.01f;
    if (stepped != expect_step)
    {
        return 2;
    }
    return 0;
}

int main()
{
    if (const int code = testPassthrough())
    {
        return code;
    }
    if (const int code = testStepResponse())
    {
        return 100 + code;
    }
    if (const int code = testWatchdogUsesRaw())
    {
        return 200 + code;
    }
    if (const int code = runChatter(0.0f, false))
    {
        return 300 + code;
    }
    if (const int code = runChatter(0.05f, true))
    {
        return 400 + code;
    }
    return 0;
}
