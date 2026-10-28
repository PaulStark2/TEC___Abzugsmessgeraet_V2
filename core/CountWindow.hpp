#pragma once

#include <cstdint>

// Sliding mean of ADC counts over a fixed time window, before the calibration
// polynomial. A stationary force with zero-mean noise is unchanged in
// expectation. The group delay is half the window.
//
// Hosts push one sample per tick. The sample must already be the mean of that
// tick (every scan in the millisecond on the STM32, one plant sample in the
// simulation). Weights are the tick lengths, so 1 ms and 50 ms describe the
// same 100 ms window.
class CountWindow
{
public:
    static constexpr int kWindowUs = 100000;
    static constexpr int kCapacity = 128;

    struct Mean
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // dt_s <= 0 leaves the previous mean in place. An empty window returns the
    // first sample, so a constant is exact before the window has filled.
    Mean push(float x, float y, float z, float dt_s);

    Mean mean() const;
    bool full() const;
    int totalUs() const;

private:
    struct Slot
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int dt_us = 0;
    };

    void dropOldest();

    Slot slots_[kCapacity]{};
    int head_ = 0;
    int count_ = 0;
    int total_us_ = 0;
};

// Nearest count in 0…4095. Telemetry only; the polynomial uses the float mean.
inline std::uint16_t roundAdcCount(float counts)
{
    if (!(counts > 0.0f))
    {
        return 0;
    }
    if (counts >= 4095.0f)
    {
        return 4095;
    }
    return static_cast<std::uint16_t>(counts + 0.5f);
}
