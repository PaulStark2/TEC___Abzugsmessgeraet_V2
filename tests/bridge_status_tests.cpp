// Unit tests for M13 StatusSnapshot mapping.
#include "SimHostInputProvider.hpp"
#include "StatusSnapshot.hpp"

#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    Machine machine;
    TestExecutor executor(machine);
    SimHostInputProvider provider;

    machine.moveTo(10.0f, 20.0f, -5.0f);
    HostInputs inputs = provider.build(machine);
    machine.setHostInputs(inputs);

    tecomm::StatusReportPayload status = simbridge::buildStatusReport(machine, executor, 0);

    if (status.machine_state != tecomm::ProtocolMachineState::Active)
    {
        return 1;
    }
    if (status.process_state != tecomm::ProtocolProcessState::Idle)
    {
        return 2;
    }
    if (!nearEqual(status.x, machine.x()) || !nearEqual(status.y, machine.y()) ||
        !nearEqual(status.z, machine.z()))
    {
        return 3;
    }
    if (!nearEqual(status.fx, inputs.force.fx) || !nearEqual(status.fy, inputs.force.fy) ||
        !nearEqual(status.fz, inputs.force.fz))
    {
        return 4;
    }
    if (status.e_stop != 0 || status.limits != 0)
    {
        return 5;
    }
    if (status.current_point_index != tecomm::kNoCurrentPointIndex)
    {
        return 6;
    }

    machine.setSpreaderExtend(true);
    machine.setFixtureExtend(true);
    const HostOutputs outputs = machine.hostOutputs();
    status = simbridge::buildStatusReport(machine, executor, 3);
    if (status.spreader_extend != 1 || status.spreader_extended != static_cast<uint8_t>(outputs.spreaderExtended ? 1 : 0))
    {
        return 7;
    }
    if (status.fixture_extend != 1)
    {
        return 8;
    }
    if (status.program_point_count != 3)
    {
        return 9;
    }

    TestProgram program;
    program.add(TestPoint{1.0f, 0.0f, 0.0f});
    program.add(TestPoint{2.0f, 0.0f, 0.0f});
    executor.setProgram(program);
    executor.start();

    machine.setHostInputs(provider.build(machine));
    status = simbridge::buildStatusReport(machine, executor, 2);
    if (status.process_state != tecomm::ProtocolProcessState::Running)
    {
        return 10;
    }
    if (status.current_point_index != 0)
    {
        return 11;
    }

    provider.setEStop(true);
    provider.setLimitXMin(true);
    machine.setHostInputs(provider.build(machine));
    status = simbridge::buildStatusReport(machine, executor, 2);
    if (status.e_stop != 1)
    {
        return 12;
    }
    if ((status.limits & (1u << tecomm::kLimitBitXMin)) == 0)
    {
        return 13;
    }

    return 0;
}
