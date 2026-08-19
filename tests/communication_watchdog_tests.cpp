#include "ForceSample.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

static HostInputs forceWithoutNewHeartbeat(std::uint32_t frozen_heartbeat)
{
    HostInputs in = withFreshForce(HostInputs{});
    in.hostHeartbeatValid = true;
    in.hostHeartbeatSequence = frozen_heartbeat;
    return in;
}

int main()
{
    // Manual/offline: watchdog off, missing heartbeat does not fault.
    {
        Machine m;
        m.moveTo(5.0f, 0.0f, 0.0f);
        tickWithForce(m, 1.0f);
        if (m.machineState() == MachineState::Fault)
        {
            return 1;
        }
        if (m.communicationWatchdogEnabled())
        {
            return 2;
        }
    }

    // Heartbeat present while watchdog enabled → no fault.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.communication.heartbeat_timeout_s = 0.2f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 10;
        }
        m.setCommunicationWatchdogEnabled(true);
        m.moveTo(4.0f, 0.0f, 0.0f);
        for (int i = 0; i < 20; ++i)
        {
            tickWithForce(m, 0.05f);
        }
        if (m.machineState() == MachineState::Fault)
        {
            return 11;
        }
        if (m.lastFaultReason() != FaultReason::None)
        {
            return 12;
        }
    }

    // Heartbeat stops → CommunicationTimeout, motion stop, spreader safe, no auto-resume.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.communication.heartbeat_timeout_s = 0.2f;
        cfg.spreader.extend_time_s = 0.5f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 20;
        }
        m.setSpreaderExtend(true);
        m.setCommunicationWatchdogEnabled(true);
        m.moveTo(8.0f, 0.0f, 0.0f);
        HostInputs first = withFreshForce(HostInputs{});
        m.setHostInputs(first);
        m.tick(0.05f);
        const std::uint32_t frozen = first.hostHeartbeatSequence;
        for (int i = 0; i < 10 && m.machineState() != MachineState::Fault; ++i)
        {
            m.setHostInputs(forceWithoutNewHeartbeat(frozen));
            m.tick(0.05f);
        }
        if (m.machineState() != MachineState::Fault ||
            m.lastFaultReason() != FaultReason::CommunicationTimeout)
        {
            return 21;
        }
        if (m.motionState() == MotionState::Moving)
        {
            return 22;
        }
        if (m.hostOutputs().spreaderExtend || m.spreaderBusy())
        {
            return 23;
        }
        if (m.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 24;
        }

        for (int i = 0; i < 5; ++i)
        {
            tickWithForce(m, 0.05f);
        }
        if (m.machineState() != MachineState::Fault)
        {
            return 25;
        }
        if (m.motionState() == MotionState::Moving)
        {
            return 26;
        }
    }

    // Automatic process: missed heartbeat aborts and stops.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 0.1f;
        tcfg.mode = TestMode::Aging;
        ConfigValidationError err = ConfigValidationError::Ok;
        machine.trySetTestConfiguration(tcfg, err);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.communication.heartbeat_timeout_s = 0.15f;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 30;
        }
        homeMachine(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{3.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 31;
        }
        HostInputs first = withFreshForce(HostInputs{});
        machine.setHostInputs(first);
        ex.tick(0.05f);
        const std::uint32_t frozen = first.hostHeartbeatSequence;
        for (int i = 0; i < 20 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(forceWithoutNewHeartbeat(frozen));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 32;
        }
        if (machine.machineState() != MachineState::Fault ||
            machine.lastFaultReason() != FaultReason::CommunicationTimeout)
        {
            return 33;
        }
        if (machine.motionState() == MotionState::Moving)
        {
            return 34;
        }
        tickWithForce(machine, 0.05f);
        ex.tick(0.05f);
        if (ex.processState() != ProcessState::Aborted)
        {
            return 35;
        }
    }

    return 0;
}
