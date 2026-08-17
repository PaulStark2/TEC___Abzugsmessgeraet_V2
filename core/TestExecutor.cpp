#include "TestExecutor.hpp"

TestExecutor::TestExecutor(Machine& machine)
    : controller_(machine)
{
}

void TestExecutor::setProgram(const TestProgram& program)
{
    controller_.setProgram(program);
}

bool TestExecutor::start()
{
    return controller_.start();
}

bool TestExecutor::startProbeCalibration()
{
    return controller_.startProbeCalibration();
}

void TestExecutor::tick(float dt)
{
    controller_.tick(dt);
}

void TestExecutor::pause()
{
    controller_.pause();
}

void TestExecutor::resume()
{
    controller_.resume();
}

void TestExecutor::stop()
{
    controller_.stop();
}

ProcessState TestExecutor::processState() const
{
    return controller_.processState();
}

ProcedurePhase TestExecutor::procedurePhase() const
{
    return controller_.phase();
}

bool TestExecutor::pauseAllowed() const
{
    return controller_.pauseAllowed();
}

std::size_t TestExecutor::currentIndex() const
{
    return controller_.currentIndex();
}

bool TestExecutor::currentPoint(TestPoint& out) const
{
    return controller_.currentPoint(out);
}

ProcedureController& TestExecutor::procedure()
{
    return controller_;
}

const ProcedureController& TestExecutor::procedure() const
{
    return controller_;
}
