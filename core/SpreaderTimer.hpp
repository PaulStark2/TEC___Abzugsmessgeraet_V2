#pragma once

#include "Types.hpp"

// Non-blocking spreader timer for a double-acting pair (open/close).
// Command is set immediately; extended() follows MachineConfig times.
// There is no end-position sensor: extended() is a timer latch, not hardware.
// STOP/Fault freezes command_ (firmware keeps the last PC8/PC12 pair).
class SpreaderTimer
{
public:
    void commandExtend(float extend_time_s)
    {
        safe_deenergized_ = false;
        command_ = true;
        if (extended_)
        {
            remaining_s_ = 0.0f;
            pending_extended_ = true;
            return;
        }
        remaining_s_ = (extend_time_s > 0.0f) ? extend_time_s : 0.0f;
        pending_extended_ = true;
        if (remaining_s_ == 0.0f)
        {
            extended_ = true;
        }
    }

    void commandRetract(float retract_time_s)
    {
        safe_deenergized_ = false;
        command_ = false;
        if (!extended_)
        {
            remaining_s_ = 0.0f;
            pending_extended_ = false;
            return;
        }
        remaining_s_ = (retract_time_s > 0.0f) ? retract_time_s : 0.0f;
        pending_extended_ = false;
        if (remaining_s_ == 0.0f)
        {
            extended_ = false;
        }
    }

    // STOP / Fault / E-Stop: freeze the last valve command. Do not finish a
    // pending extend or retract, and do not rewrite the last confirmed latch.
    // Firmware keeps the last PC8/PC12 pair; it must not treat this as close.
    void enterSafeState()
    {
        remaining_s_ = 0.0f;
        safe_deenergized_ = true;
    }

    void tick(float dt)
    {
        if (dt <= 0.0f || remaining_s_ <= 0.0f)
        {
            return;
        }
        remaining_s_ -= dt;
        if (remaining_s_ > 0.0f)
        {
            return;
        }
        remaining_s_ = 0.0f;
        extended_ = pending_extended_;
    }

    bool command() const
    {
        return command_;
    }

    bool extended() const
    {
        return extended_;
    }

    bool isBusy() const
    {
        return remaining_s_ > 0.0f;
    }

    float remainingSeconds() const
    {
        return remaining_s_;
    }

    SpreaderState state() const
    {
        if (safe_deenergized_)
        {
            return SpreaderState::SafeDeenergized;
        }
        if (remaining_s_ > 0.0f)
        {
            return command_ ? SpreaderState::Extending : SpreaderState::Retracting;
        }
        return extended_ ? SpreaderState::Extended : SpreaderState::Retracted;
    }

private:
    bool command_ = false;
    bool extended_ = false;
    bool pending_extended_ = false;
    bool safe_deenergized_ = false;
    float remaining_s_ = 0.0f;
};
