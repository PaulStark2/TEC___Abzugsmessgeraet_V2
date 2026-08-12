#include "SimHostInputProvider.hpp"

// M9 deterministic 3D force stub [N]: fx=kx*x, fy=ky*y, fz=kz*z (not a physical sensor model).
static ForceVector demoForceFromPose(float x, float y, float z)
{
    constexpr float kx = 1.0f;
    constexpr float ky = 1.0f;
    constexpr float kz = 1.0f;
    return ForceVector{kx * x, ky * y, kz * z};
}

HostInputs SimHostInputProvider::build(const Machine& machine) const
{
    HostInputs inputs{};
    inputs.force = demoForceFromPose(machine.x(), machine.y(), machine.z());
    inputs.e_stop = e_stop_;
    inputs.limitXMin = limit_x_min_;
    inputs.limitXMax = limit_x_max_;
    inputs.limitYMin = limit_y_min_;
    inputs.limitYMax = limit_y_max_;
    inputs.limitZMin = limit_z_min_;
    inputs.limitZMax = limit_z_max_;
    return inputs;
}

void SimHostInputProvider::setEStop(bool active)
{
    e_stop_ = active;
}

void SimHostInputProvider::setLimitXMin(bool active)
{
    limit_x_min_ = active;
}

void SimHostInputProvider::setLimitXMax(bool active)
{
    limit_x_max_ = active;
}

void SimHostInputProvider::setLimitYMin(bool active)
{
    limit_y_min_ = active;
}

void SimHostInputProvider::setLimitYMax(bool active)
{
    limit_y_max_ = active;
}

void SimHostInputProvider::setLimitZMin(bool active)
{
    limit_z_min_ = active;
}

void SimHostInputProvider::setLimitZMax(bool active)
{
    limit_z_max_ = active;
}
