#include "ForceCalibration.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;

static void expect(bool condition, const char* name)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", name);
        g_failures += 1;
    }
}

static bool near(double a, double b, double epsilon)
{
    return std::fabs(a - b) <= epsilon;
}

static ForceCalibPoint point(uint16_t adc, double mass_g, double nominal_g, ForceCalibDirection direction)
{
    ForceCalibPoint sample{};
    sample.adc_net = adc;
    sample.mass_g = static_cast<float>(mass_g);
    sample.nominal_g = static_cast<float>(nominal_g);
    sample.direction = direction;
    return sample;
}

static void testGrams()
{
    const double expected = (500.0 / 1000.0) * kForceCalibStandardGravity;
    expect(near(gramsToSensorNewton(500.0, ForceCalibDirection::Tension), -expected, 1e-9), "tension 500 g");
    expect(near(gramsToSensorNewton(500.0, ForceCalibDirection::Compression), expected, 1e-9), "compression 500 g");
    expect(gramsToSensorNewton(0.0, ForceCalibDirection::Zero) == 0.0, "zero mass");
}

static void testWindow()
{
    expect(referenceMassAccepted(400.0, 500.0, ForceCalibDirection::Tension), "400 g in window");
    expect(referenceMassAccepted(600.0, 500.0, ForceCalibDirection::Compression), "600 g in window");
    expect(!referenceMassAccepted(399.0, 500.0, ForceCalibDirection::Tension), "399 g rejected");
    expect(!referenceMassAccepted(601.0, 500.0, ForceCalibDirection::Tension), "601 g rejected");
    expect(referenceMassAccepted(0.0, 0.0, ForceCalibDirection::Zero), "zero point");
    expect(!referenceMassAccepted(10.0, 0.0, ForceCalibDirection::Zero), "zero point with mass");
}

static void testLineRecovery()
{
    // mass = adc/10 grams, so F = (g0/10000) * adc after the Core conversion.
    ForceCalibPoint samples[4];
    const uint16_t adcs[] = {1000, 2000, 3000, 4000};
    for (int i = 0; i < 4; ++i)
    {
        const double mass = static_cast<double>(adcs[i]) / 10.0;
        samples[i] = point(adcs[i], mass, mass, ForceCalibDirection::Compression);
    }
    const ForceCalibReport report =
        fitForceCalibration(samples, 4, ForceCurveSource::Auto, false, kForceCalibDefaultSpan_N);
    const double slope = kForceCalibStandardGravity / 10000.0;
    expect(report.committed, "line committed");
    expect(report.resolved == ForceCurveSource::Linear, "line selected");
    expect(near(report.active.beta0, 0.0, 1e-6), "line beta0");
    expect(near(report.active.beta1, slope, 1e-9), "line beta1");
    expect(report.active.beta2 == 0.0, "line beta2");
    expect(near(report.active.pearson_r, 1.0, 1e-9), "line pearson");
}

static void testQuadraticPreferred()
{
    ForceCalibPoint samples[11];
    for (int i = 0; i < 11; ++i)
    {
        const double adc = 500.0 + 300.0 * i;
        const double force = 0.005 * adc + 2.0e-7 * adc * adc;
        const double mass = std::fabs(force) / kForceCalibStandardGravity * 1000.0;
        samples[i] = point(static_cast<uint16_t>(adc), mass, mass, ForceCalibDirection::Compression);
    }
    const ForceCalibReport report =
        fitForceCalibration(samples, 11, ForceCurveSource::Auto, false, kForceCalibDefaultSpan_N);
    expect(report.committed, "quadratic committed");
    expect(report.resolved == ForceCurveSource::Quadratic, "quadratic selected");
    expect(near(report.active.beta1, 0.005, 1e-6), "quadratic beta1");
    expect(near(report.active.beta2, 2.0e-7, 1e-12), "quadratic beta2");
    expect(report.active.max_abs_residual_N < 1e-6, "quadratic residual");
}

static void testAutoRejectsLargeResidual()
{
    ForceCalibPoint samples[6];
    for (int i = 0; i < 5; ++i)
    {
        const double adc = 1000.0 + 100.0 * i;
        const double force = 0.01 * adc;
        const double mass = force / kForceCalibStandardGravity * 1000.0;
        samples[i] = point(static_cast<uint16_t>(adc), mass, mass, ForceCalibDirection::Compression);
    }
    samples[5] = point(1600, 5000.0, 5000.0, ForceCalibDirection::Compression);
    const ForceCalibReport report =
        fitForceCalibration(samples, 6, ForceCurveSource::Auto, true, 120.0);
    expect(!report.committed, "auto does not commit a large residual");
    expect(report.reason == ForceCalibReason::ResidualTooLarge, "residual reason");
    expect(!report.needs_confirmation, "auto has no manual override");
}

static void testManualConfirm()
{
    ForceCalibPoint samples[6];
    for (int i = 0; i < 5; ++i)
    {
        const double adc = 1000.0 + 100.0 * i;
        const double force = 0.01 * adc;
        const double mass = force / kForceCalibStandardGravity * 1000.0;
        samples[i] = point(static_cast<uint16_t>(adc), mass, mass, ForceCalibDirection::Compression);
    }
    samples[5] = point(1600, 5000.0, 5000.0, ForceCalibDirection::Compression);
    const ForceCalibReport blocked =
        fitForceCalibration(samples, 6, ForceCurveSource::Linear, false, 120.0);
    expect(!blocked.committed && blocked.needs_confirmation, "manual asks for confirmation");
    const ForceCalibReport accepted =
        fitForceCalibration(samples, 6, ForceCurveSource::Linear, true, 120.0);
    expect(accepted.committed, "manual confirm commits");
    expect(accepted.resolved == ForceCurveSource::Linear, "manual stays on the line");
}

static void testVoltageConversion()
{
    const double k = kForceCalibVref / static_cast<double>(kForceCalibAdcFullScale);
    const ForceCalibReport report = externalForceCalibration(1.0, 2.0, 3.0, ForceExternalUnit::Volt, nullptr, 0);
    expect(report.committed, "volt equation committed");
    expect(near(report.active.beta0, 1.0, 1e-12), "volt beta0");
    expect(near(report.active.beta1, 2.0 * k, 1e-12), "volt beta1");
    expect(near(report.active.beta2, 3.0 * k * k, 1e-15), "volt beta2");
    const ForceCalibReport falling = externalForceCalibration(0.0, -1.0, 0.0, ForceExternalUnit::AdcCounts, nullptr, 0);
    expect(!falling.committed, "falling curve rejected");
    expect(falling.reason == ForceCalibReason::NonMonotonic, "falling reason");
}

static void testRangeRejectsHeavyPoint()
{
    ForceCalibPoint samples[2];
    samples[0] = point(1000, 0.0, 0.0, ForceCalibDirection::Zero);
    samples[1] = point(2000, 5000.0, 5000.0, ForceCalibDirection::Compression);
    const ForceCalibReport report =
        fitForceCalibration(samples, 2, ForceCurveSource::Auto, false, kForceCalibDefaultSpan_N);
    expect(!report.committed, "point above the measuring range is not committed");
    expect(report.reason == ForceCalibReason::BadPoint, "range reason");
}

static void testMassWindowRejectsFit()
{
    ForceCalibPoint samples[2];
    samples[0] = point(1000, 0.0, 0.0, ForceCalibDirection::Zero);
    samples[1] = point(2000, 800.0, 500.0, ForceCalibDirection::Tension);
    const ForceCalibReport report = fitForceCalibration(samples, 2, ForceCurveSource::Auto, false);
    expect(!report.committed, "out of window not committed");
    expect(report.reason == ForceCalibReason::MassWindow, "window reason");
}

int main()
{
    testGrams();
    testWindow();
    testLineRecovery();
    testQuadraticPreferred();
    testAutoRejectsLargeResidual();
    testManualConfirm();
    testVoltageConversion();
    testRangeRejectsHeavyPoint();
    testMassWindowRejectsFit();
    if (g_failures != 0)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("force_calibration_tests ok\n");
    return 0;
}
