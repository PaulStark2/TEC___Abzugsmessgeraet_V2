#pragma once

#include "Machine.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cstddef>

// TestExecutor sequences a TestProgram through Machine without owning motion logic.
class TestExecutor
{
public:
    // Bind to an existing Machine (no ownership). // ctor
    explicit TestExecutor(Machine& machine);

    // Replace the stored program copy (fixed-size, no heap). // set data
    void setProgram(const TestProgram& program);

    // Start sequencing from Idle/Completed/Aborted if the program is non-empty. // start
    bool start();

    // Non-blocking process step; drives Machine::tick when Running. // tick
    void tick(float dt);

    // Pause/resume motion via Machine; ProcessState stays Running. // pause/resume
    void pause();
    void resume();

    // Stop machine and abort the process. // stop
    void stop();

    // Query process state. // getter
    ProcessState processState() const;

    // Index of the point currently commanded (meaningful while Running). // getter
    std::size_t currentIndex() const;

    // Copy the current program point if a valid index exists. // getter
    bool currentPoint(TestPoint& out) const;

private:
    // Command Machine to the point at current_index_. // helper
    void commandCurrentPoint();

    // Advance to the next point or complete when AtTarget. // helper
    void advanceIfAtTarget();

    Machine& machine_; // non-owning reference
    TestProgram program_; // fixed copy of the active program
    ProcessState process_state_; // process layer state
    std::size_t current_index_; // active point index while Running
};
