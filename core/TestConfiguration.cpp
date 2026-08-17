#include "TestConfiguration.hpp"

ConfigValidationError validateTestConfiguration(const TestConfiguration& config)
{
    if (!(config.target_force_N > 0.0f) || config.target_force_N > SafetyLimits::kMaxFzAbs_N)
    {
        return ConfigValidationError::TargetForceInvalid;
    }
    if (config.iterations == 0u || config.iterations > SafetyLimits::kMaxIterations)
    {
        return ConfigValidationError::IterationsZero;
    }
    if (config.mode != TestMode::Test && config.mode != TestMode::Aging)
    {
        return ConfigValidationError::TargetForceInvalid;
    }
    return ConfigValidationError::Ok;
}

bool tryApplyTestConfiguration(TestConfiguration& dest, const TestConfiguration& incoming,
                               ConfigValidationError& error)
{
    error = validateTestConfiguration(incoming);
    if (error != ConfigValidationError::Ok)
    {
        return false;
    }
    dest = incoming;
    return true;
}
