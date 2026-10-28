#include "PlateRotation.hpp"
#include "TestPoint.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    // Ideal L at K2=(0,0): K1=(0,55), K3=(55,0) → yaw 0, lengths 55, ortho 0.
    {
        const PlateRotationEval e =
            evaluatePlateRotation(TestPoint{0.0f, 55.0f, 0.0f}, TestPoint{0.0f, 0.0f, 0.0f},
                                  TestPoint{55.0f, 0.0f, 0.0f});
        if (!nearEqual(e.L12, 55.0f) || !nearEqual(e.L23, 55.0f))
        {
            return 1;
        }
        if (!nearEqual(e.yaw_rad, 0.0f) || !nearEqual(e.ortho_err_rad, 0.0f, 1e-3f))
        {
            return 2;
        }
        const PlateRotationDecision d = decidePlateRotation(e, PlateRotationThresholds{});
        if (!d.may_apply || d.length != PlateRotationVerdict::Ok ||
            !nearEqual(d.recommended_yaw_rad, 0.0f))
        {
            return 3;
        }
    }

    // Same L rotated +5° about Z around K2.
    {
        constexpr float kDeg = 5.0f * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(kDeg);
        const float s = std::sin(kDeg);
        const TestPoint k2{10.0f, 20.0f, 1.0f};
        const TestPoint k1{k2.x + (-s * 55.0f), k2.y + (c * 55.0f), 1.0f}; // Rz*(0,55)
        const TestPoint k3{k2.x + (c * 55.0f), k2.y + (s * 55.0f), 1.0f}; // Rz*(55,0)
        const PlateRotationEval e = evaluatePlateRotation(k1, k2, k3);
        if (!nearEqual(e.L12, 55.0f, 1e-3f) || !nearEqual(e.L23, 55.0f, 1e-3f))
        {
            return 4;
        }
        if (!nearEqual(e.yaw_rad, kDeg, 1e-3f))
        {
            return 5;
        }
        PlateRotationThresholds t{};
        t.tol_yaw_ignore_rad = 0.01f * 3.14159265358979323846f / 180.0f;
        t.tol_yaw_apply_rad = 0.2f * 3.14159265358979323846f / 180.0f;
        const PlateRotationDecision d = decidePlateRotation(e, t);
        if (!d.may_apply || !nearEqual(d.recommended_yaw_rad, kDeg, 1e-3f))
        {
            return 6;
        }
    }

    // Step-loss reject on length.
    {
        const PlateRotationEval e =
            evaluatePlateRotation(TestPoint{0.0f, 58.0f, 0.0f}, TestPoint{0.0f, 0.0f, 0.0f},
                                  TestPoint{55.0f, 0.0f, 0.0f});
        const PlateRotationDecision d = decidePlateRotation(e, PlateRotationThresholds{});
        if (d.may_apply || d.length != PlateRotationVerdict::Reject)
        {
            return 7;
        }
    }

    return 0;
}
