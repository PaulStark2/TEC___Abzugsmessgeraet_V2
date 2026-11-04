#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

#include <cstdio>

int main()
{
    Machine machine;
    homeMachine(machine);
    const float standoff = machine.machineConfig().probe.approach_distance;
    machine.moveTo(40.f, 40.f, 5.f + standoff);
    for (int i = 0; i < 800 && machine.motionState() != MotionState::AtTarget; ++i)
    {
        machine.setHostInputs(withFreshForce(HostInputs{}));
        machine.tick(0.05f);
    }
    std::printf("at hub pose=(%.3f,%.3f,%.3f) hard=%.1f\n", machine.x(), machine.y(), machine.z(),
                machine.machineConfig().force.hard_limit_N);

    TestExecutor ex(machine);
    if (!ex.startProbeCalibration())
    {
        std::printf("start failed\n");
        return 1;
    }
    std::printf("after start: process=%d phase=%d fault=%u busy=%d ext=%d\n",
                static_cast<int>(ex.processState()), static_cast<int>(ex.procedurePhase()),
                static_cast<unsigned>(machine.lastFaultReason()),
                static_cast<int>(machine.spreaderBusy()),
                static_cast<int>(machine.hostOutputs().spreaderExtended));

    HostInputs in = withProcedurePlant(ex, machine);
    std::printf("plant fx=%.3f fy=%.3f fz=%.3f\n", in.force.fx, in.force.fy, in.force.fz);
    machine.setHostInputs(in);
    ex.tick(0.05f);
    std::printf("after tick: process=%d phase=%d fault=%u mstate=%d trip=%u\n",
                static_cast<int>(ex.processState()), static_cast<int>(ex.procedurePhase()),
                static_cast<unsigned>(machine.lastFaultReason()),
                static_cast<int>(machine.machineState()),
                static_cast<unsigned>(machine.lastForceWatchdogTrip()));
    return 0;
}
