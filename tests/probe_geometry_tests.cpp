#include "ProbeGeometry.hpp"

#include <cmath>
#include <limits>

static bool nearEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    TestPoint offset{};
    if (!computeToolOffset(TestPoint{10.0f, 20.0f, 5.0f}, TestPoint{9.5f, 20.25f, 4.0f}, offset) ||
        !nearEqual(offset.x, 0.5f) || !nearEqual(offset.y, -0.25f) || !nearEqual(offset.z, 1.0f))
    {
        return 1;
    }

    TestPoint bad{};
    if (computeToolOffset(TestPoint{1.0f, 2.0f, 3.0f},
                          TestPoint{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, bad))
    {
        return 2;
    }

    return 0;
}
