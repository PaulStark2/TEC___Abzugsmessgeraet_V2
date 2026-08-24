#include "ProcessForceModel.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    constexpr float z_test = 100.0f;
    constexpr float k = 5.0f;

    {
        const float target = 25.0f;
        const float z_free = simplant::processForceFreeZ(z_test, target, k);
        if (!nearEqual(z_free, z_test + 5.0f))
        {
            return 1;
        }
        if (simplant::processFzZ(z_test, z_test, target, k, false, false) != 0.0f)
        {
            return 2;
        }
        if (!nearEqual(simplant::processFzZ(z_test, z_test, target, k, true, false), -target))
        {
            return 3;
        }
        if (!nearEqual(simplant::processFzZ(z_free, z_test, target, k, true, false), 0.0f))
        {
            return 4;
        }
        if (!nearEqual(simplant::processFzZ(z_free, z_test, target, k, true, true), 0.0f))
        {
            return 5;
        }
        if (!nearEqual(simplant::processFzZ(z_free - 1.0f, z_test, target, k, true, true), 5.0f))
        {
            return 6;
        }
        if (!nearEqual(simplant::processFzZ(z_free - 5.0f, z_test, target, k, true, true), 25.0f))
        {
            return 7;
        }
        if (!nearEqual(simplant::processFzZ(z_free - 10.0f, z_test, target, k, true, true), 50.0f))
        {
            return 8;
        }
        if (simplant::processFzZ(z_free + 10.0f, z_test, target, k, true, true) != 0.0f)
        {
            return 9;
        }
    }

    {
        const float targets[] = {1.0f, 25.0f, 50.0f};
        for (float target : targets)
        {
            const float at_test = simplant::processFzZ(z_test, z_test, target, k, true, false);
            if (!nearEqual(at_test, -target))
            {
                return 10;
            }
        }
    }

    return 0;
}
