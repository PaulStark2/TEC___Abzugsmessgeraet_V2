#pragma once

#include "HostInputs.hpp"
#include "Machine.hpp"

// M13 simulation source for HostInputs (no safety evaluation here).
// Since M15 the protocol host uses simplant::SimPlant as virtual outside world;
// this manual injector remains as the M9/M13 input stub.
class SimHostInputProvider
{
public:
    // Build HostInputs from current machine pose and configured simulation flags.
    HostInputs build(const Machine& machine) const;

    void setEStop(bool active);
    void setLimitXMin(bool active);
    void setLimitXMax(bool active);
    void setLimitYMin(bool active);
    void setLimitYMax(bool active);
    void setLimitZMin(bool active);
    void setLimitZMax(bool active);

private:
    bool e_stop_ = false;
    bool limit_x_min_ = false;
    bool limit_x_max_ = false;
    bool limit_y_min_ = false;
    bool limit_y_max_ = false;
    bool limit_z_min_ = false;
    bool limit_z_max_ = false;
};
