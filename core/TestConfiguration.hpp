#pragma once

#include "SafetyLimits.hpp"
#include "Types.hpp"

#include <cstdint>

// Per-run test parameters. Not stored in the point file.
struct TestConfiguration
{
    TestMode mode = TestMode::Test; // Test records force-path; Aging does not
    float target_force_N = 10.0f; // Phase-1/2 Fz setpoint [N], tensile positive
    unsigned iterations = 1u; // Phase-3 cycles (aging or test curves)
    std::uint8_t active_point_index = 0; // first point to run in the loaded program
};

// Reject target force outside (0, kMaxFzAbs] or zero iterations.
ConfigValidationError validateTestConfiguration(const TestConfiguration& config);

// Copy incoming into dest only when valid; otherwise dest unchanged.
bool tryApplyTestConfiguration(TestConfiguration& dest, const TestConfiguration& incoming,
                               ConfigValidationError& error);
