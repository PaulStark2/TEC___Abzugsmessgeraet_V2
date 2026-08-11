// Platform-independent Core type definitions.
#pragma once

// Declare the trivial Core version query used by the simulation host.
int core_version();

// Identify each linear machine axis.
enum class AxisId
{
    // Horizontal axis X.
    X,
    // Horizontal axis Y.
    Y,
    // Vertical tensile axis Z.
    Z
};

// Position tolerance used when comparing axis position to its target.
constexpr float kPositionTolerance = 1e-5f;
