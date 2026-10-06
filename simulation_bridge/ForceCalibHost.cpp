#include "ForceCalibHost.hpp"

#include <cmath>

namespace simbridge
{

namespace
{

tecomm::ForceCalibrationResultPayload toWire(const ForceCalibReport& report)
{
    tecomm::ForceCalibrationResultPayload wire{};
    wire.requested_source = static_cast<uint8_t>(report.requested);
    wire.resolved_source = static_cast<uint8_t>(report.resolved);
    wire.committed = report.committed ? 1 : 0;
    wire.reason = static_cast<uint8_t>(report.reason);
    wire.degree = report.active.degree;
    wire.needs_confirmation = report.needs_confirmation ? 1 : 0;
    wire.worst_index =
        report.active.worst_index < 0 ? static_cast<uint8_t>(0xFF) : static_cast<uint8_t>(report.active.worst_index);
    wire.point_count = static_cast<uint8_t>(report.point_count);
    wire.beta0 = static_cast<float>(report.active.beta0);
    wire.beta1 = static_cast<float>(report.active.beta1);
    wire.beta2 = static_cast<float>(report.active.beta2);
    wire.pearson_r = static_cast<float>(report.active.pearson_r);
    wire.r_squared = static_cast<float>(report.active.r_squared);
    wire.max_abs_residual_N = static_cast<float>(report.active.max_abs_residual_N);
    wire.mean_residual_tension_N = static_cast<float>(report.mean_residual_tension_N);
    wire.mean_residual_compression_N = static_cast<float>(report.mean_residual_compression_N);
    wire.linear_max_abs_N = static_cast<float>(report.linear.max_abs_residual_N);
    wire.quadratic_max_abs_N = static_cast<float>(report.quadratic.max_abs_residual_N);
    wire.linear_r2 = static_cast<float>(report.linear.r_squared);
    wire.quadratic_r2 = static_cast<float>(report.quadratic.r_squared);
    wire.linear_beta0 = static_cast<float>(report.linear.beta0);
    wire.linear_beta1 = static_cast<float>(report.linear.beta1);
    wire.linear_beta2 = static_cast<float>(report.linear.beta2);
    wire.linear_valid = report.linear.valid ? 1.0f : 0.0f;
    wire.quadratic_beta0 = static_cast<float>(report.quadratic.beta0);
    wire.quadratic_beta1 = static_cast<float>(report.quadratic.beta1);
    wire.quadratic_beta2 = static_cast<float>(report.quadratic.beta2);
    wire.quadratic_valid = report.quadratic.valid ? 1.0f : 0.0f;
    for (int i = 0; i < report.point_count; ++i)
    {
        wire.reference_force_N[i] = static_cast<float>(report.reference_force_N[i]);
    }
    return wire;
}

ForceCurveSource sourceFromWire(uint8_t source)
{
    switch (source)
    {
    case 1:
        return ForceCurveSource::Linear;
    case 2:
        return ForceCurveSource::Quadratic;
    case 3:
        return ForceCurveSource::External;
    default:
        return ForceCurveSource::Auto;
    }
}

} // namespace

void ForceCalibHost::observeRaw(uint16_t x, uint16_t y, uint16_t z, bool valid)
{
    raw_.x = x;
    raw_.y = y;
    raw_.z = z;
    raw_.valid = valid ? 1 : 0;
}

tecomm::RawAdcPayload ForceCalibHost::raw() const
{
    return raw_;
}

bool ForceCalibHost::active() const
{
    return active_ && model_.valid;
}

double ForceCalibHost::evaluateSensorNewton(uint16_t adc_z) const
{
    return evaluateForceCalibration(model_, static_cast<double>(adc_z));
}

tecomm::ForceCalibrationResultPayload ForceCalibHost::publish(const ForceCalibReport& report, bool store_points,
                                                              const ForceCalibPoint* points, int count)
{
    if (report.committed)
    {
        model_ = report.active;
        active_ = true;
        if (store_points && points != nullptr && count > 0 && count <= kForceCalibMaxPoints)
        {
            point_count_ = count;
            for (int i = 0; i < count; ++i)
            {
                points_[i] = points[i];
            }
        }
    }
    return toWire(report);
}

bool ForceCalibHost::storedForceExceeds(double range_N) const
{
    for (int i = 0; i < point_count_; ++i)
    {
        if (std::fabs(gramsToSensorNewton(points_[i].mass_g, points_[i].direction)) > range_N)
        {
            return true;
        }
    }
    return false;
}

void ForceCalibHost::clear()
{
    active_ = false;
    model_ = {};
    point_count_ = 0;
}

tecomm::ForceCalibrationResultPayload ForceCalibHost::applySet(const tecomm::SetForceCalibrationPayload& command,
                                                               double full_scale_span_N)
{
    ForceCalibPoint points[kForceCalibMaxPoints];
    const int count = static_cast<int>(command.count);
    for (int i = 0; i < count; ++i)
    {
        points[i].adc_net = command.points[i].adc_net;
        points[i].mass_g = command.points[i].mass_g;
        points[i].nominal_g = command.points[i].nominal_g;
        points[i].direction = static_cast<ForceCalibDirection>(command.points[i].direction);
    }
    const ForceCalibReport report = fitForceCalibration(points, count, sourceFromWire(command.source),
                                                        command.confirm != 0, full_scale_span_N);
    if (command.commit == 0)
    {
        ForceCalibReport preview = report;
        preview.committed = false;
        return toWire(preview);
    }
    return publish(report, true, points, count);
}

tecomm::ForceCalibrationResultPayload ForceCalibHost::applySelect(const tecomm::SelectForceCurvePayload& command,
                                                                  double full_scale_span_N)
{
    const ForceCurveSource source = sourceFromWire(command.source);
    if (source == ForceCurveSource::External)
    {
        const ForceExternalUnit unit =
            command.unit == 1 ? ForceExternalUnit::Volt : ForceExternalUnit::AdcCounts;
        const ForceCalibReport report = externalForceCalibration(command.beta0, command.beta1, command.beta2, unit,
                                                                 point_count_ > 0 ? points_ : nullptr, point_count_);
        if (command.commit == 0)
        {
            ForceCalibReport preview = report;
            preview.committed = false;
            return toWire(preview);
        }
        return publish(report, false, nullptr, 0);
    }

    if (point_count_ < 2)
    {
        ForceCalibReport report{};
        report.requested = source;
        report.reason = ForceCalibReason::NoStoredPoints;
        return toWire(report);
    }
    const ForceCalibReport report =
        fitForceCalibration(points_, point_count_, source, command.confirm != 0, full_scale_span_N);
    if (command.commit == 0)
    {
        ForceCalibReport preview = report;
        preview.committed = false;
        preview.needs_confirmation = false;
        return toWire(preview);
    }
    return publish(report, false, nullptr, 0);
}

} // namespace simbridge
