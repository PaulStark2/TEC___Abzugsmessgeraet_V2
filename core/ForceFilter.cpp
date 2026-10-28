#include "ForceFilter.hpp"

#include <cmath>

bool ForceFilter::hasSample() const
{
    return has_sample_;
}

const ForceVector& ForceFilter::value() const
{
    return y_;
}

void ForceFilter::reset(const ForceVector& sample)
{
    y_ = sample;
    has_sample_ = true;
}

ForceVector ForceFilter::update(const ForceVector& sample, float dt, float tau_s)
{
    if (!has_sample_ || !(tau_s > 0.0f) || !(dt > 0.0f))
    {
        reset(sample);
        return y_;
    }

    // Exact zero-order hold of dy/dt = (u - y) / tau over this tick.
    const float alpha = 1.0f - std::exp(-dt / tau_s);
    y_.fx += alpha * (sample.fx - y_.fx);
    y_.fy += alpha * (sample.fy - y_.fy);
    y_.fz += alpha * (sample.fz - y_.fz);
    return y_;
}
