#pragma once

#include "Machine.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

// PLACEHOLDER: stand-in for the historical probe-tip calibration protocol.
// ProcedureController talks only to this interface so the real protocol can
// replace the body later without changing the procedure state machine.
class ProbeCalibration
{
public:
    enum class State : uint8_t
    {
        Idle = 0,
        Running = 1,
        Complete = 2,
        Error = 3
    };

    bool start(Machine& machine);
    void tick(float dt, Machine& machine);
    void abort();

    bool isComplete() const;
    bool isError() const;
    State state() const;
    TestPoint result() const; // valid after Complete

private:
    State state_ = State::Idle;
    TestPoint result_{};
    bool saw_trigger_ = false;
};
