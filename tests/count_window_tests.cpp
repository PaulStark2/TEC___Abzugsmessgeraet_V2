#include "CountWindow.hpp"

#include <cmath>
#include <cstdio>

namespace
{

int fail(int code, const char* what)
{
    std::printf("count_window_tests failed %d: %s\n", code, what);
    return code;
}

} // namespace

int main()
{
    {
        CountWindow window;
        const CountWindow::Mean first = window.push(1234.5f, 10.0f, -3.0f, 0.001f);
        if (std::fabs(first.x - 1234.5f) > 1e-3f || std::fabs(first.y - 10.0f) > 1e-3f ||
            std::fabs(first.z - (-3.0f)) > 1e-3f)
        {
            return fail(1, "first sample must be returned unchanged");
        }
        if (window.full())
        {
            return fail(2, "one millisecond is not a full window");
        }
    }

    {
        CountWindow window;
        const CountWindow::Mean held = window.push(1.0f, 2.0f, 3.0f, 0.0f);
        if (held.x != 0.0f || window.totalUs() != 0)
        {
            return fail(3, "non-positive dt must not invent a sample");
        }
    }

    {
        CountWindow window;
        CountWindow::Mean mean{};
        for (int i = 0; i < 200; ++i)
        {
            const float noise = (i % 2 == 0) ? 40.0f : -40.0f;
            mean = window.push(2000.0f + noise, 1000.0f + noise, 3000.0f - noise, 0.001f);
        }
        if (!window.full())
        {
            return fail(4, "100 ms of 1 ms ticks must fill the window");
        }
        if (std::fabs(mean.x - 2000.0f) > 0.05f || std::fabs(mean.y - 1000.0f) > 0.05f ||
            std::fabs(mean.z - 3000.0f) > 0.05f)
        {
            return fail(5, "zero-mean alternation must not bias the count");
        }
    }

    {
        CountWindow window;
        CountWindow::Mean mean{};
        // 50 Hz at 1 kHz is exactly 20 samples per cycle. Five cycles fit the window,
        // and the sum of sin(2π k/20) over any 100 consecutive samples is zero.
        constexpr double kAmplitude = 68.0;
        for (int i = 0; i < 250; ++i)
        {
            const double s = kAmplitude * std::sin(2.0 * 3.14159265358979323846 * static_cast<double>(i) / 20.0);
            mean = window.push(static_cast<float>(2048.0 + s), static_cast<float>(2048.0 + s),
                               static_cast<float>(2048.0 - s), 0.001f);
        }
        if (std::fabs(mean.x - 2048.0f) > 0.5f || std::fabs(mean.y - 2048.0f) > 0.5f ||
            std::fabs(mean.z - 2048.0f) > 0.5f)
        {
            std::printf("50 Hz residual x=%f y=%f z=%f\n", static_cast<double>(mean.x),
                        static_cast<double>(mean.y), static_cast<double>(mean.z));
            return fail(6, "a full window must null 50 Hz and its harmonics");
        }
    }

    {
        CountWindow window;
        for (int i = 0; i < 100; ++i)
        {
            (void)window.push(0.0f, 0.0f, 0.0f, 0.001f);
        }
        CountWindow::Mean mean{};
        for (int i = 0; i < 50; ++i)
        {
            mean = window.push(100.0f, 100.0f, 100.0f, 0.001f);
        }
        if (std::fabs(mean.x - 50.0f) > 0.05f)
        {
            std::printf("half step mean=%f\n", static_cast<double>(mean.x));
            return fail(7, "50 ms into a step must sit at half the step");
        }
    }

    {
        CountWindow window;
        const CountWindow::Mean half =
            window.push(0.0f, 0.0f, 0.0f, 0.05f);
        (void)half;
        const CountWindow::Mean mean = window.push(100.0f, 40.0f, -20.0f, 0.05f);
        if (!window.full() || std::fabs(mean.x - 50.0f) > 1e-3f || std::fabs(mean.y - 20.0f) > 1e-3f ||
            std::fabs(mean.z - (-10.0f)) > 1e-3f)
        {
            return fail(8, "two 50 ms slots must cover the same 100 ms as 1 ms ticks");
        }
        const CountWindow::Mean settled = window.push(100.0f, 40.0f, -20.0f, 0.05f);
        if (std::fabs(settled.x - 100.0f) > 1e-3f || std::fabs(settled.y - 40.0f) > 1e-3f ||
            std::fabs(settled.z - (-20.0f)) > 1e-3f)
        {
            return fail(9, "a held step must settle after one window");
        }
    }

    if (roundAdcCount(-1.0f) != 0 || roundAdcCount(0.4f) != 0 || roundAdcCount(0.5f) != 1 ||
        roundAdcCount(2048.4f) != 2048 || roundAdcCount(2048.5f) != 2049 || roundAdcCount(5000.0f) != 4095)
    {
        return fail(10, "telemetry rounding");
    }

    std::printf("count_window_tests ok\n");
    return 0;
}
