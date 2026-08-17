#pragma once

// Non-blocking spreader valve timer. Command (valve) is set immediately;
// Extended feedback follows after MachineConfig spreader times.
class SpreaderTimer
{
public:
    void commandExtend(float extend_time_s)
    {
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

private:
    bool command_ = false;
    bool extended_ = false;
    bool pending_extended_ = false;
    float remaining_s_ = 0.0f;
};
