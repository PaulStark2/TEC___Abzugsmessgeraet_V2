#pragma once

#include "HostInputs.hpp"

// Rest sample (tara). Twenty force readings, 100 ms apart, become the zero
// subtracted from later Fx/Fy/Fz. The calibration curve is not modified.
class ForceTare
{
public:
    static constexpr int kSampleCount = 20;
    static constexpr float kSampleGapS = 0.1f;

    bool active() const;
    bool engaged() const;
    ForceVector offset() const;

    // Kennlinien-Newton minus the last committed mean. Identity until the first sample finishes.
    ForceVector apply(const ForceVector& raw) const;

    // First reading is taken immediately from raw. Further readings come from tick.
    bool begin(const ForceVector& raw);

    // Drop an in-progress capture. A committed offset stays.
    void cancel();

    // Returns true on the tick that commits the mean.
    bool tick(float dt, const ForceVector& raw);

private:
    void take(const ForceVector& raw);

    bool active_ = false;
    bool engaged_ = false;
    int count_ = 0;
    float accum_s_ = 0.0f;
    ForceVector sum_{0.0f, 0.0f, 0.0f};
    ForceVector offset_{0.0f, 0.0f, 0.0f};
};
