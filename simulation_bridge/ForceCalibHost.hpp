#pragma once

#include "ForceCalibration.hpp"
#include "Payloads.hpp"

#include <cstdint>

namespace simbridge
{

// Holds the active Z measurement function and the latest raw ADC sample.
// The Core machine still receives Newton only.
class ForceCalibHost
{
public:
    void observeRaw(uint16_t x, uint16_t y, uint16_t z, bool valid);
    tecomm::RawAdcPayload raw() const;

    tecomm::ForceCalibrationResultPayload applySet(const tecomm::SetForceCalibrationPayload& command,
                                                    double full_scale_span_N = kForceCalibDefaultSpan_N);
    tecomm::ForceCalibrationResultPayload applySelect(const tecomm::SelectForceCurvePayload& command,
                                                       double full_scale_span_N = kForceCalibDefaultSpan_N);

    bool storedForceExceeds(double range_N) const;
    void clear();

    bool active() const;
    double evaluateSensorNewton(uint16_t adc_z) const;

private:
    tecomm::ForceCalibrationResultPayload publish(const ForceCalibReport& report, bool store_points,
                                                  const ForceCalibPoint* points, int count);

    ForceCalibPoint points_[kForceCalibMaxPoints]{};
    int point_count_ = 0;
    ForceCalibModel model_{};
    bool active_ = false;
    tecomm::RawAdcPayload raw_{};
};

} // namespace simbridge
