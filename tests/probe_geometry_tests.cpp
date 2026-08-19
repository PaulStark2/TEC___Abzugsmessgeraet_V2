#include "CoordinateTransform.hpp"
#include "ProbeGeometry.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    const float probe_d = 8.0f;
    const float thick = 3.0f;
    const float tool_d = 2.5f;
    const float r_sum = expectedXyContactRadius(probe_d, tool_d);

    // Contact → center for each lateral direction.
    {
        TestPoint center{};
        if (!contactToProbeCenter(TestPoint{10.0f - r_sum, 20.0f, 5.0f}, ProbeSearchDir::NegX, probe_d,
                                  thick, tool_d, center) ||
            !nearEqual(center.x, 10.0f) || !nearEqual(center.y, 20.0f))
        {
            return 1;
        }
        if (!contactToProbeCenter(TestPoint{10.0f + r_sum, 20.0f, 5.0f}, ProbeSearchDir::PosX, probe_d,
                                  thick, tool_d, center) ||
            !nearEqual(center.x, 10.0f))
        {
            return 2;
        }
        if (!contactToProbeCenter(TestPoint{10.0f, 20.0f - r_sum, 5.0f}, ProbeSearchDir::NegY, probe_d,
                                  thick, tool_d, center) ||
            !nearEqual(center.y, 20.0f))
        {
            return 3;
        }
        if (!contactToProbeCenter(TestPoint{10.0f, 20.0f + r_sum, 5.0f}, ProbeSearchDir::PosY, probe_d,
                                  thick, tool_d, center) ||
            !nearEqual(center.y, 20.0f))
        {
            return 4;
        }
        if (!contactToProbeCenter(TestPoint{10.0f, 20.0f, 6.5f}, ProbeSearchDir::NegZ, probe_d, thick,
                                  tool_d, center) ||
            !nearEqual(center.z, 5.0f))
        {
            return 5;
        }
    }

    // Different tool diameter changes the conversion, not the disc size.
    {
        TestPoint a{};
        TestPoint b{};
        contactToProbeCenter(TestPoint{0.0f, 0.0f, 0.0f}, ProbeSearchDir::NegX, probe_d, thick, 2.5f, a);
        contactToProbeCenter(TestPoint{0.0f, 0.0f, 0.0f}, ProbeSearchDir::NegX, probe_d, thick, 5.0f, b);
        if (nearEqual(a.x, b.x))
        {
            return 6;
        }
        if (!nearEqual(b.x - a.x, 1.25f))
        {
            return 7;
        }
    }

    // Four opposing TCP contacts → disc center. Radius cancels in the midpoint.
    {
        const TestPoint neg_x{10.0f - r_sum, 20.0f, 5.0f};
        const TestPoint pos_x{10.0f + r_sum, 20.0f, 5.0f};
        const TestPoint neg_y{10.0f, 20.0f - r_sum, 5.0f};
        const TestPoint pos_y{10.0f, 20.0f + r_sum, 5.0f};
        TestPoint center{};
        float radius = 0.0f;
        if (!fitCircleCenterXY(neg_x, pos_x, neg_y, pos_y, r_sum, 0.5f, center, radius))
        {
            return 10;
        }
        if (!nearEqual(center.x, 10.0f) || !nearEqual(center.y, 20.0f) || !nearEqual(radius, r_sum))
        {
            return 11;
        }
    }

    // Degenerate: all points the same → error.
    {
        const TestPoint p{1.0f, 1.0f, 1.0f};
        TestPoint center{};
        float radius = 0.0f;
        if (fitCircleCenterXY(p, p, p, p, r_sum, 0.5f, center, radius))
        {
            return 12;
        }
    }

    // Unrealistic radius vs expected → error.
    {
        const TestPoint neg_x{0.0f, 0.0f, 0.0f};
        const TestPoint pos_x{40.0f, 0.0f, 0.0f};
        const TestPoint neg_y{20.0f, -20.0f, 0.0f};
        const TestPoint pos_y{20.0f, 20.0f, 0.0f};
        TestPoint center{};
        float radius = 0.0f;
        if (fitCircleCenterXY(neg_x, pos_x, neg_y, pos_y, r_sum, 0.5f, center, radius))
        {
            return 13;
        }
    }

    // Offset = nominal - measured, then TCP_nominal + offset.
    {
        const TestPoint nominal{100.0f, 50.0f, 20.0f};
        const TestPoint measured{101.0f, 48.0f, 21.0f};
        TestPoint offset{};
        if (!computeToolOffset(nominal, measured, offset))
        {
            return 20;
        }
        if (!nearEqual(offset.x, -1.0f) || !nearEqual(offset.y, 2.0f) || !nearEqual(offset.z, -1.0f))
        {
            return 21;
        }
        CalibrationSet set{};
        set.plate_origin = TestPoint{0.0f, 0.0f, 0.0f};
        set.plate_origin_valid = true;
        set.tool_offset = offset;
        TestPoint tcp_nominal{200.0f, 100.0f, 50.0f};
        TestPoint tcp{};
        if (!toMachinePoint(set, tcp_nominal, tcp))
        {
            return 22;
        }
        if (!nearEqual(tcp.x, 199.0f) || !nearEqual(tcp.y, 102.0f) || !nearEqual(tcp.z, 49.0f))
        {
            return 23;
        }
        TestPoint offset2{};
        computeToolOffset(nominal, measured, offset2);
        if (!nearEqual(offset2.x, offset.x) || !nearEqual(offset2.y, offset.y))
        {
            return 24;
        }
    }

    // NaN rejected.
    {
        TestPoint offset{};
        TestPoint nan_pt{NAN, 0.0f, 0.0f};
        if (computeToolOffset(nan_pt, TestPoint{1.0f, 1.0f, 1.0f}, offset))
        {
            return 25;
        }
    }

    // Disc contact: top face from above.
    {
        if (!toolContactsProbeDisc(0.0f, 0.0f, 6.5f, 0.0f, 0.0f, 5.0f, probe_d, thick, tool_d))
        {
            return 30;
        }
        if (toolContactsProbeDisc(0.0f, 0.0f, 7.0f, 0.0f, 0.0f, 5.0f, probe_d, thick, tool_d))
        {
            return 31;
        }
        if (!toolContactsProbeDisc(-r_sum, 0.0f, 5.0f, 0.0f, 0.0f, 5.0f, probe_d, thick, tool_d))
        {
            return 32;
        }
        if (toolContactsProbeDisc(-r_sum - 0.2f, 0.0f, 5.0f, 0.0f, 0.0f, 5.0f, probe_d, thick, tool_d))
        {
            return 33;
        }
    }

    if (!nearEqual(xyStartOffset(10.0f, 8.0f, 2.5f), 15.25f))
    {
        return 40;
    }
    if (!nearEqual(maxXySearchRadius(8.0f, 2.0f), 6.0f) || !nearEqual(xySearchInnerRadius(8.0f, 2.0f), 2.0f))
    {
        return 41;
    }

    // Spreader cylinder 15 mm from TCP in +Z; XY contact 5 mm below disc center is valid.
    {
        const float height = 15.0f;
        const float cz = 0.0f;
        const float tcp_z = cz - 5.0f;
        if (!toolContactsProbeDisc(-r_sum, 0.0f, tcp_z, 0.0f, 0.0f, cz, probe_d, thick, tool_d, height))
        {
            return 50;
        }
        if (toolContactsProbeDisc(-r_sum, 0.0f, cz - 20.0f, 0.0f, 0.0f, cz, probe_d, thick, tool_d, height))
        {
            return 51;
        }
        if (toolContactsProbeDisc(-r_sum - 0.2f, 0.0f, tcp_z, 0.0f, 0.0f, cz, probe_d, thick, tool_d, height))
        {
            return 52;
        }
    }

    return 0;
}
