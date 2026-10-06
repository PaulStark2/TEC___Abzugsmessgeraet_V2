#pragma once

#include <cstddef>
#include <cstdint>

// Z-force calibration math. Platform independent: no HAL, no ADC hardware.
// The host supplies raw counts and reference masses. This module converts
// grams to sensor Newtons, fits a least-squares line and a quadratic
// polynomial, and chooses which one becomes the measurement function.

constexpr int kForceCalibMaxPointsPerDirection = 16;
constexpr int kForceCalibMaxPoints = 1 + 2 * kForceCalibMaxPointsPerDirection;

// 12-bit full scale and reference voltage. Same numbers as the firmware
// AdcScaler; duplicated here so Core does not include a firmware header.
constexpr int kForceCalibAdcFullScale = 4095;
constexpr double kForceCalibVref = 3.3;

// Standard gravity used for every gram-to-newton conversion.
constexpr double kForceCalibStandardGravity = 9.80665;

// Operator entry must lie within this many grams of the prompted step.
constexpr double kForceCalibMassWindow_g = 100.0;

// Default span is twice the default measuring range (±30 N → 60 N).
// The host passes the configured span. 0.3 % of that span is the practical
// bar for accepting a quadratic term. 1 % rejects an automatic fit.
constexpr double kForceCalibDefaultSpan_N = 60.0;
constexpr double kForceCalibNonlinearityFraction = 0.003;
constexpr double kForceCalibRejectFraction = 0.01;

// Quadratic model is only eligible once three coefficients leave residual
// degrees of freedom. Below this count the line is the only candidate.
constexpr int kForceCalibMinPointsForQuadratic = 6;

enum class ForceCurveSource : uint8_t
{
    Auto = 0,
    Linear = 1,
    Quadratic = 2,
    External = 3,
};

enum class ForceCalibDirection : uint8_t
{
    Tension = 0,
    Compression = 1,
    Zero = 2,
};

enum class ForceExternalUnit : uint8_t
{
    AdcCounts = 0,
    Volt = 1,
};

enum class ForceCalibReason : uint8_t
{
    Ok = 0,
    TooFewPoints = 1,
    MassWindow = 2,
    NonMonotonic = 3,
    ResidualTooLarge = 4,
    NoStoredPoints = 5,
    InvalidCoefficients = 6,
    SingularFit = 7,
    BadPoint = 8,
};

// One wizard sample after adapter tare. mass_g is the typed positive mass.
// nominal_g is the prompted step (0 for the unloaded point).
struct ForceCalibPoint
{
    uint16_t adc_net = 0;
    float mass_g = 0.0f;
    float nominal_g = 0.0f;
    ForceCalibDirection direction = ForceCalibDirection::Zero;
};

// Measurement function F_sensor[N] = beta0 + beta1*adc + beta2*adc^2.
// Tension is negative. Coefficients are in raw ADC counts, not scaled units.
struct ForceCalibModel
{
    bool valid = false;
    uint8_t degree = 0;
    double beta0 = 0.0;
    double beta1 = 0.0;
    double beta2 = 0.0;
    double pearson_r = 0.0;
    double r_squared = 0.0;
    double max_abs_residual_N = 0.0;
    int worst_index = -1;
    double ss_res = 0.0;
    bool monotonic = false;
};

struct ForceCalibReport
{
    ForceCalibModel linear{};
    ForceCalibModel quadratic{};
    ForceCalibModel active{};
    ForceCurveSource requested = ForceCurveSource::Auto;
    // Linear, Quadratic, or External. Auto is resolved before it is stored here.
    ForceCurveSource resolved = ForceCurveSource::Auto;
    bool committed = false;
    bool needs_confirmation = false;
    ForceCalibReason reason = ForceCalibReason::Ok;
    double mean_residual_tension_N = 0.0;
    double mean_residual_compression_N = 0.0;
    // Sensor-frame reference force of each accepted point, same order as input.
    double reference_force_N[kForceCalibMaxPoints]{};
    int point_count = 0;
};

// Positive grams become a sensor-frame Newton value. Tension is negative.
double gramsToSensorNewton(double mass_g, ForceCalibDirection direction);

// True when the typed mass may be stored for this step.
bool referenceMassAccepted(double mass_g, double nominal_g, ForceCalibDirection direction);

// F_sensor from a model at one ADC count.
double evaluateForceCalibration(const ForceCalibModel& model, double adc_counts);

// Strictly rising derivative on the closed ADC interval.
bool forceCurveMonotonicRising(double beta1, double beta2, double adc_lo, double adc_hi);

// Fit both models and resolve `requested`. full_scale_span_N is 2 * measuring range.
// confirm_large_residual lets a manual line or quadratic through the 1 % bar.
ForceCalibReport fitForceCalibration(const ForceCalibPoint* points, int count, ForceCurveSource requested,
                                     bool confirm_large_residual, double full_scale_span_N = kForceCalibDefaultSpan_N);

// External equation. Volt coefficients are rewritten onto ADC counts.
// points may be null. Residuals against points are reported and do not reject.
ForceCalibReport externalForceCalibration(double beta0, double beta1, double beta2, ForceExternalUnit unit,
                                          const ForceCalibPoint* points, int count);
