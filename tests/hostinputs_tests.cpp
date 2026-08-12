// Deterministic unit tests for HostInputs / ForceVector (Milestone 9).
#include "HostInputs.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

static bool sameForce(const ForceVector& a, const ForceVector& b)
{
    return nearEqual(a.fx, b.fx) && nearEqual(a.fy, b.fy) && nearEqual(a.fz, b.fz);
}

int main()
{
    // Default ForceVector is zero.
    {
        ForceVector f{};
        if (!nearEqual(f.fx, 0.0f) || !nearEqual(f.fy, 0.0f) || !nearEqual(f.fz, 0.0f))
        {
            return 1;
        }
    }

    // Default HostInputs force is zero.
    {
        HostInputs in{};
        if (!sameForce(in.force, ForceVector{0.0f, 0.0f, 0.0f}))
        {
            return 2;
        }
    }

    // Positive components.
    {
        HostInputs in{};
        in.force = {10.0f, 20.0f, 30.0f};
        if (!sameForce(in.force, ForceVector{10.0f, 20.0f, 30.0f}))
        {
            return 3;
        }
    }

    // Negative components.
    {
        HostInputs in{};
        in.force = {-5.0f, -2.5f, -100.0f};
        if (!sameForce(in.force, ForceVector{-5.0f, -2.5f, -100.0f}))
        {
            return 4;
        }
    }

    // Mixed signs.
    {
        HostInputs in{};
        in.force = {1.0f, -2.0f, 3.0f};
        if (!sameForce(in.force, ForceVector{1.0f, -2.0f, 3.0f}))
        {
            return 5;
        }
    }

    // Copy assignment preserves force.
    {
        HostInputs a{};
        a.force = {7.0f, -8.0f, 9.0f};
        HostInputs b = a;
        if (!sameForce(b.force, a.force))
        {
            return 6;
        }
        a.force.fx = 0.0f;
        if (nearEqual(b.force.fx, 0.0f))
        {
            return 7;
        }
    }

    return 0;
}
