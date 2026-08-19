#pragma once

#include "TestPoint.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdint>

// Search direction toward the probe disc. NegX means the tool starts on -X and
// travels toward +X until contact (the -X rim of the disc).
enum class ProbeSearchDir : uint8_t
{
    NegX = 0,
    PosX = 1,
    NegY = 2,
    PosY = 3,
    NegZ = 4
};

// Lateral start offset from the nominal center. head_diameter is intentionally
// not used (reserved, default 0).
inline float xyStartOffset(float search_distance, float measuring_head_diameter, float tool_diameter)
{
    return search_distance + 0.5f * measuring_head_diameter + 0.5f * tool_diameter;
}

inline float probeRadius(float measuring_head_diameter)
{
    return 0.5f * measuring_head_diameter;
}

inline float toolRadius(float tool_diameter)
{
    return 0.5f * tool_diameter;
}

inline float expectedXyContactRadius(float measuring_head_diameter, float tool_diameter)
{
    return probeRadius(measuring_head_diameter) + toolRadius(tool_diameter);
}

// Named quantity from the M19.0 correction spec: (R_probe + margin).
//
// Important: this is a *diagnostic* named quantity, not the commanded inner
// TCP radius. The actual motion stop is xySearchInnerRadius() (inner TCP
// radius toward the probe axis).
inline float maxXySearchRadius(float measuring_head_diameter, float xy_search_margin)
{
    return probeRadius(measuring_head_diameter) + xy_search_margin;
}

// Hard XY motion bound: innermost |TCP_xy − probe_axis| that may be commanded.
// Search starts outside (search_distance + radii) and moves toward the axis.
// Contact is at R_probe + R_tool. Using maxXySearchRadius (R_probe + margin) as
// that inner radius would stop at 6 mm and never reach contact at 5.25 mm.
// The commanded stop is therefore max(0, R_probe − margin): 2 mm past the disc
// radius toward the axis, never r = 0. Tool radius is not in this formula.
inline float xySearchInnerRadius(float measuring_head_diameter, float xy_search_margin)
{
    const float inner = probeRadius(measuring_head_diameter) - xy_search_margin;
    return inner > 0.0f ? inner : 0.0f;
}

// TCP at contact → estimated probe-disc center contribution for one direction.
// Z uses disc thickness (center is mid-plane), not a spherical head radius.
inline bool contactToProbeCenter(const TestPoint& tcp, ProbeSearchDir dir, float measuring_head_diameter,
                                 float measuring_head_thickness, float tool_diameter, TestPoint& center_out)
{
    if (!std::isfinite(tcp.x) || !std::isfinite(tcp.y) || !std::isfinite(tcp.z) ||
        !std::isfinite(measuring_head_diameter) || !std::isfinite(measuring_head_thickness) ||
        !std::isfinite(tool_diameter) || measuring_head_diameter < 0.0f || measuring_head_thickness < 0.0f ||
        tool_diameter < 0.0f)
    {
        return false;
    }

    const float r_sum = expectedXyContactRadius(measuring_head_diameter, tool_diameter);
    center_out = tcp;
    switch (dir)
    {
    case ProbeSearchDir::NegX:
        center_out.x = tcp.x + r_sum;
        return true;
    case ProbeSearchDir::PosX:
        center_out.x = tcp.x - r_sum;
        return true;
    case ProbeSearchDir::NegY:
        center_out.y = tcp.y + r_sum;
        return true;
    case ProbeSearchDir::PosY:
        center_out.y = tcp.y - r_sum;
        return true;
    case ProbeSearchDir::NegZ:
        center_out.z = tcp.z - 0.5f * measuring_head_thickness;
        return true;
    default:
        return false;
    }
}

// Specialized 4-point center: midpoints of opposite TCP contacts. Expected
// radius is probe_r + tool_r. Degenerate spans or radius mismatch → false.
inline bool fitCircleCenterXY(const TestPoint& neg_x, const TestPoint& pos_x, const TestPoint& neg_y,
                              const TestPoint& pos_y, float expected_radius, float radius_tolerance,
                              TestPoint& center_out, float& radius_out)
{
    if (!std::isfinite(neg_x.x) || !std::isfinite(pos_x.x) || !std::isfinite(neg_y.y) ||
        !std::isfinite(pos_y.y) || !std::isfinite(expected_radius) || !std::isfinite(radius_tolerance) ||
        expected_radius <= 0.0f || radius_tolerance < 0.0f)
    {
        return false;
    }

    const float span_x = pos_x.x - neg_x.x;
    const float span_y = pos_y.y - neg_y.y;
    if (!(span_x > kPositionTolerance) || !(span_y > kPositionTolerance))
    {
        return false;
    }

    center_out.x = 0.5f * (neg_x.x + pos_x.x);
    center_out.y = 0.5f * (neg_y.y + pos_y.y);
    center_out.z = 0.25f * (neg_x.z + pos_x.z + neg_y.z + pos_y.z);

    const TestPoint pts[4] = {neg_x, pos_x, neg_y, pos_y};
    float radius_sum = 0.0f;
    for (const TestPoint& p : pts)
    {
        const float dx = p.x - center_out.x;
        const float dy = p.y - center_out.y;
        const float r = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(r) || r <= kPositionTolerance)
        {
            return false;
        }
        radius_sum += r;
    }
    radius_out = radius_sum * 0.25f;
    if (!std::isfinite(radius_out) || std::fabs(radius_out - expected_radius) > radius_tolerance)
    {
        return false;
    }
    return true;
}

// tool_offset = P_probe_nominal - P_probe_measured (replace, do not accumulate).
inline bool computeToolOffset(const TestPoint& nominal, const TestPoint& measured, TestPoint& offset_out)
{
    if (!std::isfinite(nominal.x) || !std::isfinite(nominal.y) || !std::isfinite(nominal.z) ||
        !std::isfinite(measured.x) || !std::isfinite(measured.y) || !std::isfinite(measured.z))
    {
        return false;
    }
    offset_out.x = nominal.x - measured.x;
    offset_out.y = nominal.y - measured.y;
    offset_out.z = nominal.z - measured.z;
    return std::isfinite(offset_out.x) && std::isfinite(offset_out.y) && std::isfinite(offset_out.z);
}

// Vertical tool cylinder from TCP upward intersecting the probe disc volume.
inline bool toolContactsProbeDisc(float tool_x, float tool_y, float tool_z, float probe_x, float probe_y,
                                  float probe_z, float measuring_head_diameter, float measuring_head_thickness,
                                  float tool_diameter, float tool_height = 15.0f)
{
    if (!std::isfinite(tool_x) || !std::isfinite(tool_y) || !std::isfinite(tool_z) || !std::isfinite(probe_x) ||
        !std::isfinite(probe_y) || !std::isfinite(probe_z) || measuring_head_diameter < 0.0f ||
        measuring_head_thickness <= 0.0f || tool_diameter < 0.0f || tool_height <= 0.0f)
    {
        return false;
    }

    const float dx = tool_x - probe_x;
    const float dy = tool_y - probe_y;
    const float dist_xy = std::sqrt(dx * dx + dy * dy);
    const float r_sum = expectedXyContactRadius(measuring_head_diameter, tool_diameter);
    if (dist_xy > r_sum)
    {
        return false;
    }

    const float disc_z_min = probe_z - 0.5f * measuring_head_thickness;
    const float disc_z_max = probe_z + 0.5f * measuring_head_thickness;
    const float tool_z_max = tool_z + tool_height;
    return tool_z <= disc_z_max && tool_z_max >= disc_z_min;
}
