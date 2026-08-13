#pragma once

#include "HostInputs.hpp"

namespace simplant
{

// Default probe pulse length [s] (short digital pulse, then automatically LOW).
constexpr float kDefaultProbePulseSeconds = 0.1f;

// Trip points of the virtual max limit switches [machine position units].
struct PlantLimits
{
    float x_max = 100.0f;
    float y_max = 100.0f;
    float z_max = 50.0f;
};

// Read-back snapshot of the virtual sensors for the simulation channel.
struct PlantState
{
    PlantLimits limits;
    bool limit_x_max = false;
    bool limit_y_max = false;
    bool limit_z_max = false;
    bool probe_triggered = false;
    bool e_stop = false;
    float probe_pulse_seconds = kDefaultProbePulseSeconds;
};

// SimPlant is the virtual outside world of the PC simulation (M15).
//
// It maps a controller-reported pose to sensor signals and hands them to the
// Core as HostInputs. It deliberately contains NO controller logic:
//  - it never stops motion when a limit switch trips (Core safety decides),
//  - it never integrates axis positions (MotionPlanner/Axis own that),
//  - it reports zero force in M15 (no force simulation yet).
class SimPlant
{
public:
    void setLimits(const PlantLimits& limits);
    PlantLimits limits() const;

    // Configure the probe pulse length; values <= 0 keep the current setting.
    void setProbePulseSeconds(float seconds);

    // Arm one probe pulse: the sensor reads HIGH until the pulse elapsed.
    void triggerProbe();

    void setEStop(bool active);
    bool eStop() const;

    // Advance sensor timers only (probe pulse). Not a physics step.
    void advance(float dt);

    // Sensor signals for the given controller pose (force is always zero).
    HostInputs sample(float x, float y, float z) const;

    // Current sensor state for the given pose (diagnostics / simulation UI).
    PlantState state(float x, float y, float z) const;

private:
    PlantLimits limits_{};
    float probe_pulse_seconds_ = kDefaultProbePulseSeconds;
    float probe_remaining_ = 0.0f;
    bool e_stop_ = false;
};

} // namespace simplant
