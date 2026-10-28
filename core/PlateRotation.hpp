#pragma once

#include "TestPoint.hpp"

#include <cmath>
#include <cstdint>

// Evaluate K1–K2–K3 L-shape for plate yaw about Z.
// Convention: K2→K1 = plate +Y (0, +L), K2→K3 = plate +X (+L, 0). Nominal edge length 55 mm.
struct PlateRotationEval
{
    float L12 = 0.0f;
    float L23 = 0.0f;
    float theta_x_rad = 0.0f; // angle of K2→K3 vs machine +X
    float theta_y_rad = 0.0f; // angle of K2→K1 vs machine +Y
    float yaw_rad = 0.0f; // mean of theta_x and theta_y
    float ortho_err_rad = 0.0f; // |angle(V12,V23) − π/2|
    float yaw_split_rad = 0.0f; // |theta_x − theta_y|
    float z_span_mm = 0.0f; // max|z| among K1/K2/K3 vs their mean (tilt hint)
};

inline float wrapPi(float a)
{
    constexpr float kPi = 3.14159265358979323846f;
    while (a > kPi)
    {
        a -= 2.0f * kPi;
    }
    while (a < -kPi)
    {
        a += 2.0f * kPi;
    }
    return a;
}

inline PlateRotationEval evaluatePlateRotation(const TestPoint& k1, const TestPoint& k2,
                                               const TestPoint& k3)
{
    PlateRotationEval e{};
    const float v12x = k1.x - k2.x;
    const float v12y = k1.y - k2.y;
    const float v23x = k3.x - k2.x;
    const float v23y = k3.y - k2.y;
    e.L12 = std::hypot(v12x, v12y);
    e.L23 = std::hypot(v23x, v23y);
    // Signed angle of measured plate-+Y from machine-+Y (CCW positive).
    e.theta_y_rad = std::atan2(-v12x, v12y);
    // Signed angle of measured plate-+X from machine-+X (CCW positive).
    e.theta_x_rad = std::atan2(v23y, v23x);
    e.yaw_rad = 0.5f * (e.theta_x_rad + e.theta_y_rad);
    e.yaw_split_rad = std::fabs(wrapPi(e.theta_x_rad - e.theta_y_rad));

    const float dot = v12x * v23x + v12y * v23y;
    const float cross = v12x * v23y - v12y * v23x;
    const float ang = std::atan2(cross, dot); // signed angle V12→V23
    constexpr float kHalfPi = 1.57079632679489661923f;
    e.ortho_err_rad = std::fabs(std::fabs(ang) - kHalfPi);

    const float z_mean = (k1.z + k2.z + k3.z) / 3.0f;
    e.z_span_mm = std::fmax(std::fabs(k1.z - z_mean),
                            std::fmax(std::fabs(k2.z - z_mean), std::fabs(k3.z - z_mean)));
    return e;
}

struct PlateRotationThresholds
{
    float nom_edge_mm = 55.0f;
    float tol_length_warn_mm = 0.3f;
    float tol_length_reject_mm = 1.0f;
    float tol_yaw_ignore_rad = 0.05f * 3.14159265358979323846f / 180.0f;
    float tol_yaw_apply_rad = 0.2f * 3.14159265358979323846f / 180.0f;
    float tol_ortho_warn_rad = 0.2f * 3.14159265358979323846f / 180.0f;
    float tol_ortho_reject_rad = 1.0f * 3.14159265358979323846f / 180.0f;
    float tol_parallel_warn_rad = 0.2f * 3.14159265358979323846f / 180.0f;
};

enum class PlateRotationVerdict : std::uint8_t
{
    Ok = 0,
    Warn = 1,
    Reject = 2
};

struct PlateRotationDecision
{
    PlateRotationVerdict length = PlateRotationVerdict::Ok;
    PlateRotationVerdict ortho = PlateRotationVerdict::Ok;
    PlateRotationVerdict parallel = PlateRotationVerdict::Ok;
    PlateRotationVerdict yaw = PlateRotationVerdict::Ok;
    float recommended_yaw_rad = 0.0f; // 0 if below ignore
    bool may_apply = true;
};

inline PlateRotationDecision decidePlateRotation(const PlateRotationEval& e,
                                                 const PlateRotationThresholds& t)
{
    PlateRotationDecision d{};
    const float d12 = std::fabs(e.L12 - t.nom_edge_mm);
    const float d23 = std::fabs(e.L23 - t.nom_edge_mm);
    const float d_len = d12 > d23 ? d12 : d23;
    if (d_len >= t.tol_length_reject_mm)
    {
        d.length = PlateRotationVerdict::Reject;
        d.may_apply = false;
    }
    else if (d_len >= t.tol_length_warn_mm)
    {
        d.length = PlateRotationVerdict::Warn;
    }

    if (e.ortho_err_rad >= t.tol_ortho_reject_rad)
    {
        d.ortho = PlateRotationVerdict::Reject;
        d.may_apply = false;
    }
    else if (e.ortho_err_rad >= t.tol_ortho_warn_rad)
    {
        d.ortho = PlateRotationVerdict::Warn;
    }

    if (e.yaw_split_rad >= t.tol_parallel_warn_rad)
    {
        d.parallel = PlateRotationVerdict::Warn;
    }

    const float abs_yaw = std::fabs(e.yaw_rad);
    if (abs_yaw < t.tol_yaw_ignore_rad)
    {
        d.yaw = PlateRotationVerdict::Ok;
        d.recommended_yaw_rad = 0.0f;
    }
    else if (abs_yaw >= t.tol_yaw_apply_rad)
    {
        d.yaw = PlateRotationVerdict::Warn;
        d.recommended_yaw_rad = e.yaw_rad;
    }
    else
    {
        d.yaw = PlateRotationVerdict::Ok;
        d.recommended_yaw_rad = e.yaw_rad; // optional apply
    }
    return d;
}
