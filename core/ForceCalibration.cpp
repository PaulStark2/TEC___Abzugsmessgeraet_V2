#include "ForceCalibration.hpp"

#include <cmath>

namespace
{

constexpr double kSingularPivot = 1e-14;
constexpr double kSsEpsilon = 1e-18;

// Critical values of F(1, df2) at alpha = 0.05. Index is df2.
// Source: standard F distribution, one numerator degree of freedom.
constexpr double kFCrit05[] = {
    0.0,
    161.4476, 18.5128, 10.1280, 7.7086, 6.6079, 5.9874, 5.5914, 5.3177, 5.1174, 4.9646,
    4.8443,  4.7472,  4.6672,  4.6001, 4.5431, 4.4940, 4.4513, 4.4139, 4.3807, 4.3512,
    4.3248,  4.3009,  4.2793,  4.2597, 4.2417, 4.2252, 4.2100, 4.1960, 4.1830, 4.1709,
};

double fCritical05(int df2)
{
    if (df2 < 1)
    {
        return 1.0e9;
    }
    if (df2 > 30)
    {
        return kFCrit05[30];
    }
    return kFCrit05[df2];
}

bool finitePoint(const ForceCalibPoint& point)
{
    return std::isfinite(point.mass_g) && std::isfinite(point.nominal_g);
}

bool solveNormal(double a[][3], double rhs[], double x[], int n)
{
    // Gaussian elimination with partial pivoting on the normal equations.
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        double best = std::fabs(a[col][col]);
        for (int row = col + 1; row < n; ++row)
        {
            const double value = std::fabs(a[row][col]);
            if (value > best)
            {
                best = value;
                pivot = row;
            }
        }
        if (best < kSingularPivot)
        {
            return false;
        }
        if (pivot != col)
        {
            for (int k = 0; k < n; ++k)
            {
                const double tmp = a[col][k];
                a[col][k] = a[pivot][k];
                a[pivot][k] = tmp;
            }
            const double tmp_rhs = rhs[col];
            rhs[col] = rhs[pivot];
            rhs[pivot] = tmp_rhs;
        }
        const double diag = a[col][col];
        for (int row = col + 1; row < n; ++row)
        {
            const double factor = a[row][col] / diag;
            for (int k = col; k < n; ++k)
            {
                a[row][k] -= factor * a[col][k];
            }
            rhs[row] -= factor * rhs[col];
        }
    }
    for (int row = n - 1; row >= 0; --row)
    {
        double sum = rhs[row];
        for (int k = row + 1; k < n; ++k)
        {
            sum -= a[row][k] * x[k];
        }
        if (std::fabs(a[row][row]) < kSingularPivot)
        {
            return false;
        }
        x[row] = sum / a[row][row];
    }
    return true;
}

void betasFromScaled(const double* scaled, int degree, double mean, double scale, ForceCalibModel& model)
{
    // u = (adc - mean) / scale, then rewrite F(u) as a polynomial in adc.
    const double inv_s = 1.0 / scale;
    if (degree == 1)
    {
        model.degree = 1;
        model.beta2 = 0.0;
        model.beta1 = scaled[1] * inv_s;
        model.beta0 = scaled[0] - model.beta1 * mean;
        return;
    }
    model.degree = 2;
    model.beta2 = scaled[2] * inv_s * inv_s;
    model.beta1 = scaled[1] * inv_s - 2.0 * mean * model.beta2;
    model.beta0 = scaled[0] - scaled[1] * inv_s * mean + model.beta2 * mean * mean;
}

bool fitDegree(const double* adc, const double* force, int count, int degree, double mean, double scale,
               ForceCalibModel& model)
{
    const int n = degree + 1;
    double matrix[3][3] = {};
    double rhs[3] = {};
    double scaled[3] = {};
    for (int i = 0; i < count; ++i)
    {
        const double u = (adc[i] - mean) / scale;
        double power = 1.0;
        double powers[3];
        for (int p = 0; p < n; ++p)
        {
            powers[p] = power;
            power *= u;
        }
        for (int row = 0; row < n; ++row)
        {
            rhs[row] += powers[row] * force[i];
            for (int col = 0; col < n; ++col)
            {
                matrix[row][col] += powers[row] * powers[col];
            }
        }
    }
    if (!solveNormal(matrix, rhs, scaled, n))
    {
        model.valid = false;
        return false;
    }
    betasFromScaled(scaled, degree, mean, scale, model);
    model.valid = std::isfinite(model.beta0) && std::isfinite(model.beta1) && std::isfinite(model.beta2);
    return model.valid;
}

void scoreModel(const double* adc, const double* force, int count, const ForceCalibDirection* direction,
                ForceCalibModel& model, double adc_lo, double adc_hi)
{
    double ss_res = 0.0;
    double max_abs = 0.0;
    int worst = -1;
    for (int i = 0; i < count; ++i)
    {
        const double predicted = evaluateForceCalibration(model, adc[i]);
        const double residual = force[i] - predicted;
        const double abs_residual = std::fabs(residual);
        ss_res += residual * residual;
        if (abs_residual > max_abs)
        {
            max_abs = abs_residual;
            worst = i;
        }
        (void)direction;
    }
    double mean_force = 0.0;
    for (int i = 0; i < count; ++i)
    {
        mean_force += force[i];
    }
    mean_force /= static_cast<double>(count);
    double ss_tot = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double delta = force[i] - mean_force;
        ss_tot += delta * delta;
    }
    model.ss_res = ss_res;
    model.max_abs_residual_N = max_abs;
    model.worst_index = worst;
    if (ss_tot < kSsEpsilon)
    {
        model.r_squared = ss_res < kSsEpsilon ? 1.0 : 0.0;
    }
    else
    {
        model.r_squared = 1.0 - ss_res / ss_tot;
    }
    model.monotonic = forceCurveMonotonicRising(model.beta1, model.beta2, adc_lo, adc_hi);
}

double pearsonR(const double* adc, const double* force, int count)
{
    // r = sum((x-xm)(y-ym)) / sqrt(sum (x-xm)^2 * sum (y-ym)^2).
    double mean_x = 0.0;
    double mean_y = 0.0;
    for (int i = 0; i < count; ++i)
    {
        mean_x += adc[i];
        mean_y += force[i];
    }
    mean_x /= static_cast<double>(count);
    mean_y /= static_cast<double>(count);
    double qx = 0.0;
    double qy = 0.0;
    double qxy = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double dx = adc[i] - mean_x;
        const double dy = force[i] - mean_y;
        qx += dx * dx;
        qy += dy * dy;
        qxy += dx * dy;
    }
    if (qx < kSsEpsilon || qy < kSsEpsilon)
    {
        return 0.0;
    }
    return qxy / std::sqrt(qx * qy);
}

void directionalResiduals(const double* adc, const double* force, const ForceCalibDirection* direction, int count,
                          const ForceCalibModel& model, double& mean_tension, double& mean_compression)
{
    double sum_tension = 0.0;
    double sum_compression = 0.0;
    int n_tension = 0;
    int n_compression = 0;
    for (int i = 0; i < count; ++i)
    {
        const double residual = force[i] - evaluateForceCalibration(model, adc[i]);
        if (direction[i] == ForceCalibDirection::Tension)
        {
            sum_tension += residual;
            n_tension += 1;
        }
        else if (direction[i] == ForceCalibDirection::Compression)
        {
            sum_compression += residual;
            n_compression += 1;
        }
    }
    mean_tension = n_tension > 0 ? sum_tension / static_cast<double>(n_tension) : 0.0;
    mean_compression = n_compression > 0 ? sum_compression / static_cast<double>(n_compression) : 0.0;
}

bool quadraticBeatsLine(const ForceCalibModel& linear, const ForceCalibModel& quadratic, int count,
                        double full_scale_span_N)
{
    if (!quadratic.valid || !quadratic.monotonic || count < kForceCalibMinPointsForQuadratic)
    {
        return false;
    }
    const double practical = kForceCalibNonlinearityFraction * full_scale_span_N;
    if (!(linear.max_abs_residual_N > practical))
    {
        return false;
    }
    const int df2 = count - 3;
    double ss_quad = quadratic.ss_res;
    if (ss_quad < 0.0)
    {
        ss_quad = 0.0;
    }
    const double improvement = linear.ss_res - ss_quad;
    if (improvement <= 0.0)
    {
        return false;
    }
    if (ss_quad < kSsEpsilon)
    {
        return true;
    }
    const double f_stat = (improvement / 1.0) / (ss_quad / static_cast<double>(df2));
    return f_stat > fCritical05(df2);
}

} // namespace

double gramsToSensorNewton(double mass_g, ForceCalibDirection direction)
{
    const double magnitude = (mass_g / 1000.0) * kForceCalibStandardGravity;
    if (direction == ForceCalibDirection::Tension)
    {
        return -magnitude;
    }
    if (direction == ForceCalibDirection::Compression)
    {
        return magnitude;
    }
    return 0.0;
}

bool referenceMassAccepted(double mass_g, double nominal_g, ForceCalibDirection direction)
{
    if (!std::isfinite(mass_g) || !std::isfinite(nominal_g))
    {
        return false;
    }
    if (direction == ForceCalibDirection::Zero)
    {
        return mass_g == 0.0 && nominal_g == 0.0;
    }
    if (direction != ForceCalibDirection::Tension && direction != ForceCalibDirection::Compression)
    {
        return false;
    }
    if (nominal_g <= 0.0 || mass_g < 0.0)
    {
        return false;
    }
    return std::fabs(mass_g - nominal_g) <= kForceCalibMassWindow_g;
}

double evaluateForceCalibration(const ForceCalibModel& model, double adc_counts)
{
    return model.beta0 + model.beta1 * adc_counts + model.beta2 * adc_counts * adc_counts;
}

bool forceCurveMonotonicRising(double beta1, double beta2, double adc_lo, double adc_hi)
{
    if (!std::isfinite(beta1) || !std::isfinite(beta2) || !std::isfinite(adc_lo) || !std::isfinite(adc_hi))
    {
        return false;
    }
    if (adc_hi < adc_lo)
    {
        return false;
    }
    // Derivative is linear in adc, so the interval minimum is at an endpoint.
    const double slope_lo = beta1 + 2.0 * beta2 * adc_lo;
    const double slope_hi = beta1 + 2.0 * beta2 * adc_hi;
    return slope_lo > 0.0 && slope_hi > 0.0;
}

ForceCalibReport fitForceCalibration(const ForceCalibPoint* points, int count, ForceCurveSource requested,
                                     bool confirm_large_residual, double full_scale_span_N)
{
    ForceCalibReport report{};
    report.requested = requested;
    if (points == nullptr || count < 2 || count > kForceCalibMaxPoints)
    {
        report.reason = ForceCalibReason::TooFewPoints;
        return report;
    }
    if (!(full_scale_span_N > 0.0) || !std::isfinite(full_scale_span_N))
    {
        report.reason = ForceCalibReason::BadPoint;
        return report;
    }
    if (requested == ForceCurveSource::External)
    {
        report.reason = ForceCalibReason::BadPoint;
        return report;
    }

    double adc[kForceCalibMaxPoints];
    double force[kForceCalibMaxPoints];
    ForceCalibDirection direction[kForceCalibMaxPoints];
    double mean_adc = 0.0;
    for (int i = 0; i < count; ++i)
    {
        if (!finitePoint(points[i]) ||
            !referenceMassAccepted(points[i].mass_g, points[i].nominal_g, points[i].direction))
        {
            report.reason = ForceCalibReason::MassWindow;
            report.active.worst_index = i;
            return report;
        }
        adc[i] = static_cast<double>(points[i].adc_net);
        force[i] = gramsToSensorNewton(points[i].mass_g, points[i].direction);
        if (std::fabs(force[i]) > (full_scale_span_N * 0.5) + 1e-6)
        {
            report.reason = ForceCalibReason::BadPoint;
            report.active.worst_index = i;
            return report;
        }
        direction[i] = points[i].direction;
        report.reference_force_N[i] = force[i];
        mean_adc += adc[i];
    }
    report.point_count = count;
    mean_adc /= static_cast<double>(count);

    double var = 0.0;
    double adc_lo = adc[0];
    double adc_hi = adc[0];
    for (int i = 0; i < count; ++i)
    {
        const double delta = adc[i] - mean_adc;
        var += delta * delta;
        if (adc[i] < adc_lo)
        {
            adc_lo = adc[i];
        }
        if (adc[i] > adc_hi)
        {
            adc_hi = adc[i];
        }
    }
    const double scale = std::sqrt(var / static_cast<double>(count));
    if (!(scale > 0.0))
    {
        report.reason = ForceCalibReason::SingularFit;
        return report;
    }

    if (!fitDegree(adc, force, count, 1, mean_adc, scale, report.linear))
    {
        report.reason = ForceCalibReason::SingularFit;
        return report;
    }
    scoreModel(adc, force, count, direction, report.linear, adc_lo, adc_hi);
    report.linear.pearson_r = pearsonR(adc, force, count);

    if (count >= 3)
    {
        if (fitDegree(adc, force, count, 2, mean_adc, scale, report.quadratic))
        {
            scoreModel(adc, force, count, direction, report.quadratic, adc_lo, adc_hi);
            report.quadratic.pearson_r = report.linear.pearson_r;
        }
    }

    const bool want_quadratic =
        requested == ForceCurveSource::Quadratic ||
        (requested == ForceCurveSource::Auto &&
         quadraticBeatsLine(report.linear, report.quadratic, count, full_scale_span_N));

    ForceCalibModel* chosen = &report.linear;
    report.resolved = ForceCurveSource::Linear;
    if (want_quadratic)
    {
        if (!report.quadratic.valid || !report.quadratic.monotonic)
        {
            if (requested == ForceCurveSource::Quadratic)
            {
                report.reason = ForceCalibReason::NonMonotonic;
                report.active = report.quadratic;
                return report;
            }
            // Automatic mode falls back to the line when the parabola turns inside the interval.
        }
        else
        {
            chosen = &report.quadratic;
            report.resolved = ForceCurveSource::Quadratic;
        }
    }

    if (!chosen->valid || !chosen->monotonic)
    {
        report.reason = ForceCalibReason::NonMonotonic;
        report.active = *chosen;
        directionalResiduals(adc, force, direction, count, report.active, report.mean_residual_tension_N,
                             report.mean_residual_compression_N);
        return report;
    }

    report.active = *chosen;
    directionalResiduals(adc, force, direction, count, report.active, report.mean_residual_tension_N,
                         report.mean_residual_compression_N);

    const double reject = kForceCalibRejectFraction * full_scale_span_N;
    if (report.active.max_abs_residual_N > reject)
    {
        const bool manual = requested == ForceCurveSource::Linear || requested == ForceCurveSource::Quadratic;
        if (manual && confirm_large_residual)
        {
            report.committed = true;
            report.reason = ForceCalibReason::Ok;
            return report;
        }
        report.committed = false;
        report.needs_confirmation = manual;
        report.reason = ForceCalibReason::ResidualTooLarge;
        return report;
    }

    report.committed = true;
    report.reason = ForceCalibReason::Ok;
    return report;
}

ForceCalibReport externalForceCalibration(double beta0, double beta1, double beta2, ForceExternalUnit unit,
                                          const ForceCalibPoint* points, int count)
{
    ForceCalibReport report{};
    report.requested = ForceCurveSource::External;
    report.resolved = ForceCurveSource::External;
    if (!std::isfinite(beta0) || !std::isfinite(beta1) || !std::isfinite(beta2))
    {
        report.reason = ForceCalibReason::InvalidCoefficients;
        return report;
    }

    ForceCalibModel model{};
    model.valid = true;
    if (unit == ForceExternalUnit::Volt)
    {
        // V = adc * Vref / 4095, substituted into b0 + b1*V + b2*V^2.
        const double k = kForceCalibVref / static_cast<double>(kForceCalibAdcFullScale);
        model.beta0 = beta0;
        model.beta1 = beta1 * k;
        model.beta2 = beta2 * k * k;
    }
    else
    {
        model.beta0 = beta0;
        model.beta1 = beta1;
        model.beta2 = beta2;
    }
    model.degree = model.beta2 == 0.0 ? 1 : 2;
    model.monotonic = forceCurveMonotonicRising(model.beta1, model.beta2, 0.0,
                                                static_cast<double>(kForceCalibAdcFullScale));
    if (!model.monotonic)
    {
        report.reason = ForceCalibReason::NonMonotonic;
        report.active = model;
        return report;
    }

    if (points != nullptr && count > 0 && count <= kForceCalibMaxPoints)
    {
        double adc[kForceCalibMaxPoints];
        double force[kForceCalibMaxPoints];
        ForceCalibDirection direction[kForceCalibMaxPoints];
        int used = 0;
        for (int i = 0; i < count; ++i)
        {
            if (!finitePoint(points[i]))
            {
                continue;
            }
            adc[used] = static_cast<double>(points[i].adc_net);
            force[used] = gramsToSensorNewton(points[i].mass_g, points[i].direction);
            direction[used] = points[i].direction;
            report.reference_force_N[used] = force[used];
            used += 1;
        }
        report.point_count = used;
        if (used >= 2)
        {
            double adc_lo = adc[0];
            double adc_hi = adc[0];
            for (int i = 1; i < used; ++i)
            {
                if (adc[i] < adc_lo)
                {
                    adc_lo = adc[i];
                }
                if (adc[i] > adc_hi)
                {
                    adc_hi = adc[i];
                }
            }
            scoreModel(adc, force, used, direction, model, adc_lo, adc_hi);
            model.pearson_r = pearsonR(adc, force, used);
            directionalResiduals(adc, force, direction, used, model, report.mean_residual_tension_N,
                                 report.mean_residual_compression_N);
            // External curves are not rejected for residual size. Restore the
            // full-scale monotonicity decision after scoreModel overwrote it.
            model.monotonic = forceCurveMonotonicRising(model.beta1, model.beta2, 0.0,
                                                        static_cast<double>(kForceCalibAdcFullScale));
            model.valid = true;
        }
    }

    report.active = model;
    report.committed = true;
    report.reason = ForceCalibReason::Ok;
    return report;
}
