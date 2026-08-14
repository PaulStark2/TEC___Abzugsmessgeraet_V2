#pragma once

#include "HostInputs.hpp"

namespace simplant
{

constexpr float kDefaultProbePulseSeconds = 0.1f;

// Trip points of the virtual limit switches [machine position units].
struct PlantLimits
{
    float x_min = 0.0f;
    float x_max = 100.0f;
    float y_min = 0.0f;
    float y_max = 100.0f;
    float z_min = 0.0f;
    float z_max = 50.0f;
};

struct PlantState
{
    PlantLimits limits;
    bool limit_x_min = false;
    bool limit_x_max = false;
    bool limit_y_min = false;
    bool limit_y_max = false;
    bool limit_z_min = false;
    bool limit_z_max = false;
    bool probe_triggered = false;
    bool e_stop = false;
    float probe_pulse_seconds = kDefaultProbePulseSeconds;
};

class SimPlant
{
public:
    void setLimits(const PlantLimits& limits);
    PlantLimits limits() const;

    void setProbePulseSeconds(float seconds);
    void triggerProbe();

    void setEStop(bool active);
    bool eStop() const;

    void advance(float dt);

    HostInputs sample(float x, float y, float z) const;
    PlantState state(float x, float y, float z) const;

private:
    PlantLimits limits_{};
    float probe_pulse_seconds_ = kDefaultProbePulseSeconds;
    float probe_remaining_ = 0.0f;
    bool e_stop_ = false;
};

} // namespace simplant
