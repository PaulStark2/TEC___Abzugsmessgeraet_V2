#pragma once

#include "HostInputs.hpp"

// Discrete low-pass on Fx, Fy, Fz before tare is applied to the published sample.
//
// Continuous model, one time constant tau for every axis:
//   tau * dy/dt + y = u
//   Y(s)/U(s) = 1 / (1 + s*tau)
//
// Exact step over one tick of length dt, held input u:
//   y(t+dt) = u + (y(t) - u) * exp(-dt / tau)
// which is the same recurrence as
//   alpha = 1 - exp(-dt / tau)
//   y <- y + alpha * (u - y)
//
// tau <= 0 is a passthrough: y = u. The same tau is used at 1 ms (STM32) and
// 50 ms (simulation); an alpha stored per sample would not be.
class ForceFilter
{
public:
    bool hasSample() const;
    const ForceVector& value() const;

    // y = sample. The next update is one step away from this value.
    void reset(const ForceVector& sample);

    // One step toward sample. tau_s <= 0 or dt <= 0 replaces y with sample.
    ForceVector update(const ForceVector& sample, float dt, float tau_s);

private:
    bool has_sample_ = false;
    ForceVector y_{0.0f, 0.0f, 0.0f};
};
