#include "ForceTare.hpp"

bool ForceTare::active() const
{
    return active_;
}

bool ForceTare::engaged() const
{
    return engaged_;
}

ForceVector ForceTare::offset() const
{
    return offset_;
}

ForceVector ForceTare::apply(const ForceVector& raw) const
{
    if (!engaged_)
    {
        return raw;
    }
    return ForceVector{raw.fx - offset_.fx, raw.fy - offset_.fy, raw.fz - offset_.fz};
}

void ForceTare::take(const ForceVector& raw)
{
    sum_.fx += raw.fx;
    sum_.fy += raw.fy;
    sum_.fz += raw.fz;
    ++count_;
}

bool ForceTare::begin(const ForceVector& raw)
{
    if (active_)
    {
        return false;
    }
    active_ = true;
    count_ = 0;
    accum_s_ = 0.0f;
    sum_ = ForceVector{0.0f, 0.0f, 0.0f};
    take(raw);
    return true;
}

void ForceTare::cancel()
{
    active_ = false;
    count_ = 0;
    accum_s_ = 0.0f;
    sum_ = ForceVector{0.0f, 0.0f, 0.0f};
}

bool ForceTare::tick(float dt, const ForceVector& raw)
{
    if (!active_ || !(dt > 0.0f))
    {
        return false;
    }

    accum_s_ += dt;
    while (count_ < kSampleCount && accum_s_ + 1.0e-4f >= kSampleGapS)
    {
        accum_s_ -= kSampleGapS;
        if (accum_s_ < 0.0f)
        {
            accum_s_ = 0.0f;
        }
        take(raw);
    }

    if (count_ < kSampleCount)
    {
        return false;
    }

    const float n = static_cast<float>(count_);
    offset_ = ForceVector{sum_.fx / n, sum_.fy / n, sum_.fz / n};
    engaged_ = true;
    active_ = false;
    count_ = 0;
    accum_s_ = 0.0f;
    sum_ = ForceVector{0.0f, 0.0f, 0.0f};
    return true;
}
