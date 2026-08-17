#pragma once

#include "Machine.hpp"
#include "ProcedureController.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cstddef>

// Facade: one process SM (ProcedureController). Do not add a second sequencer.
class TestExecutor
{
public:
    explicit TestExecutor(Machine& machine);

    void setProgram(const TestProgram& program);
    bool start();
    bool startProbeCalibration();
    void tick(float dt);
    void pause();
    void resume();
    void stop();

    ProcessState processState() const;
    ProcedurePhase procedurePhase() const;
    bool pauseAllowed() const;
    std::size_t currentIndex() const;
    bool currentPoint(TestPoint& out) const;
    ProcedureController& procedure();
    const ProcedureController& procedure() const;

private:
    ProcedureController controller_;
};
