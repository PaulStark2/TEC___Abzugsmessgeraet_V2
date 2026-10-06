// Deterministic tests for firmware ADC 12-bit → Newton scaling (M19.x Phase 2.1).
#include "AdcScaler.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

int main()
{
    if (!nearEqual(adc_scaler::countsToNewton(0), -30.0f))
    {
        return 1;
    }
    if (!nearEqual(adc_scaler::countsToNewton(4095), 30.0f))
    {
        return 2;
    }
    if (!nearEqual(adc_scaler::countsToNewton(2048), -30.0f + (2048.0f / 4095.0f) * 60.0f))
    {
        return 3;
    }
    if (adc_scaler::countsToNewton(5000) > 30.0001f)
    {
        return 4;
    }
    if (!nearEqual(adc_scaler::countsToNewton(0, 25.0f), -25.0f))
    {
        return 5;
    }
    if (!nearEqual(adc_scaler::countsToNewton(4095, 25.0f), 25.0f))
    {
        return 6;
    }
    if (!nearEqual(adc_scaler::countsToVolt(0), 0.0f))
    {
        return 7;
    }
    if (!nearEqual(adc_scaler::countsToVolt(4095), 3.3f))
    {
        return 8;
    }
    return 0;
}
