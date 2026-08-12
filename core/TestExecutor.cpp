#include "TestExecutor.hpp"

// Construct an idle executor bound to a Machine. // ctor
TestExecutor::TestExecutor(Machine& machine)
    : machine_(machine) // store reference
    , program_() // empty program copy
    , process_state_(ProcessState::Idle) // initial process state
    , current_index_(0) // no active point
{
}

void TestExecutor::setProgram(const TestProgram& program)
{
    program_ = program; // copy fixed-capacity program
}

bool TestExecutor::start()
{
    // Reject start while already sequencing. // invalid transition
    if (process_state_ == ProcessState::Running)
    {
        return false; // unchanged
    }

    // Empty programs cannot start. // empty check
    if (program_.size() == 0)
    {
        return false; // stay Idle/terminal as-is; caller expects Idle for empty from Idle
    }

    // Reset sequencing state for a fresh run. // reset
    current_index_ = 0; // first point
    process_state_ = ProcessState::Running; // enter Running
    commandCurrentPoint(); // Machine.moveTo(point0)
    return true; // started
}

void TestExecutor::tick(float dt)
{
    // Only Running executes process logic. // guard
    if (process_state_ != ProcessState::Running)
    {
        return; // Idle/Completed/Aborted: no-op
    }

    // External or prior Machine stop aborts the process. // stop detect
    if (machine_.machineState() == MachineState::Stopped || machine_.machineState() == MachineState::Fault)
    {
        process_state_ = ProcessState::Aborted; // abort
        return; // no further motion
    }

    // While the machine is paused, do not advance process points. // pause respect
    if (machine_.machineState() == MachineState::Paused)
    {
        machine_.tick(dt); // harmless no-op while paused
        return; // no AtTarget advancement
    }

    // Advance machine motion for this time step. // drive machine
    machine_.tick(dt);

    // Stop may have been applied during tick by an external caller pattern; re-check. // recheck
    if (machine_.machineState() == MachineState::Stopped || machine_.machineState() == MachineState::Fault)
    {
        process_state_ = ProcessState::Aborted; // abort
        return; // done
    }

    // If still paused after tick, do not sequence. // pause after tick
    if (machine_.machineState() == MachineState::Paused)
    {
        return; // no advance
    }

    // Consume AtTarget by commanding the next point or completing. // sequence
    advanceIfAtTarget();
}

void TestExecutor::pause()
{
    // Pause only makes sense while the process is Running. // guard
    if (process_state_ != ProcessState::Running)
    {
        return; // no-op
    }

    machine_.pause(); // ProcessState stays Running
}

void TestExecutor::resume()
{
    // Resume only while Running (Machine may be Paused). // guard
    if (process_state_ != ProcessState::Running)
    {
        return; // no-op after Idle/Completed/Aborted
    }

    machine_.resume(); // ProcessState stays Running
}

void TestExecutor::stop()
{
    // Ignore stop when not Running to avoid unexpected Machine transitions. // guard
    if (process_state_ != ProcessState::Running)
    {
        return; // no-op in Idle/Completed/Aborted
    }

    machine_.stop(); // freeze machine
    process_state_ = ProcessState::Aborted; // abort process
}

ProcessState TestExecutor::processState() const
{
    return process_state_; // query
}

std::size_t TestExecutor::currentIndex() const
{
    return current_index_; // query
}

bool TestExecutor::currentPoint(TestPoint& out) const
{
    // No valid current point while Idle before any start. // idle
    if (process_state_ == ProcessState::Idle)
    {
        return false; // none
    }

    // Return the point at the current index if present. // lookup
    return program_.pointAt(current_index_, out);
}

void TestExecutor::commandCurrentPoint()
{
    TestPoint point{}; // scratch
    if (!program_.pointAt(current_index_, point)) // should always succeed while Running
    {
        process_state_ = ProcessState::Aborted; // defensive
        return; // abort
    }

    machine_.moveTo(point.x, point.y, point.z); // delegate motion
}

void TestExecutor::advanceIfAtTarget()
{
    // Only advance when the current move has arrived. // AtTarget gate
    if (machine_.motionState() != MotionState::AtTarget)
    {
        return; // still moving or idle freeze
    }

    // Bound the number of same-tick advances by remaining points (no busy-wait). // safety
    const std::size_t max_steps = program_.size(); // upper bound
    for (std::size_t step = 0; step < max_steps; ++step)
    {
        if (machine_.motionState() != MotionState::AtTarget) // left AtTarget after moveTo
        {
            return; // waiting on motion
        }

        if (process_state_ != ProcessState::Running) // may complete/abort inside
        {
            return; // done
        }

        // More points remain: command the next one. // next
        if (current_index_ + 1 < program_.size())
        {
            ++current_index_; // advance index
            commandCurrentPoint(); // moveTo next
            continue; // if already at next pose, loop again
        }

        // Last point reached. // complete
        process_state_ = ProcessState::Completed; // success
        return; // exit
    }
}
