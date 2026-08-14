// M16 soft limit blocking tests (per-direction, no global fault).
#include "Machine.hpp"

#include "HostInputs.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

static HostInputs makeSafeInputs()
{
    HostInputs in{};
    in.force = ForceVector{0.0f, 0.0f, 0.0f};
    in.e_stop = false;
    return in;
}

int main()
{
    constexpr float kDt = 0.02f;

    // Other axis continues while one axis is blocked at max.
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.limitXMax = true;
        m.setHostInputs(in);

        m.moveTo(10.0f, 5.0f, 0.0f);
        for (int t = 0; t < 50; ++t)
        {
            m.tick(kDt);
            if (m.machineState() == MachineState::Fault)
            {
                return 1;
            }
        }

        if (!nearEqual(m.x(), 0.0f))
        {
            return 2;
        }
        if (nearEqual(m.y(), 0.0f))
        {
            return 3;
        }
        if ((m.blockedDirections() & (1u << kLimitDirXMax)) == 0)
        {
            return 4;
        }
    }

    // All six directions block toward active limit without fault.
    {
        struct Case
        {
            bool limitXMin;
            bool limitXMax;
            bool limitYMin;
            bool limitYMax;
            bool limitZMin;
            bool limitZMax;
            float tx;
            float ty;
            float tz;
            uint8_t expected_bit;
        };

        const Case cases[] = {
            {true, false, false, false, false, false, -10.0f, 0.0f, 0.0f, 1u << kLimitDirXMin},
            {false, true, false, false, false, false, 10.0f, 0.0f, 0.0f, 1u << kLimitDirXMax},
            {false, false, true, false, false, false, 0.0f, -10.0f, 0.0f, 1u << kLimitDirYMin},
            {false, false, false, true, false, false, 0.0f, 10.0f, 0.0f, 1u << kLimitDirYMax},
            {false, false, false, false, true, false, 0.0f, 0.0f, -10.0f, 1u << kLimitDirZMin},
            {false, false, false, false, false, true, 0.0f, 0.0f, 10.0f, 1u << kLimitDirZMax},
        };

        for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            Machine m;
            HostInputs in = makeSafeInputs();
            in.limitXMin = cases[i].limitXMin;
            in.limitXMax = cases[i].limitXMax;
            in.limitYMin = cases[i].limitYMin;
            in.limitYMax = cases[i].limitYMax;
            in.limitZMin = cases[i].limitZMin;
            in.limitZMax = cases[i].limitZMax;
            m.setHostInputs(in);

            m.moveTo(cases[i].tx, cases[i].ty, cases[i].tz);
            m.tick(kDt);

            if (m.machineState() == MachineState::Fault)
            {
                return static_cast<int>(10 + i);
            }
            if ((m.blockedDirections() & cases[i].expected_bit) == 0)
            {
                return static_cast<int>(20 + i);
            }
        }
    }

    return 0;
}
