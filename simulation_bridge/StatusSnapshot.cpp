#include "StatusSnapshot.hpp"
#include "ProtocolTypes.hpp"

namespace simbridge
{

tecomm::StatusReportPayload buildStatusReport(const Machine& machine, const TestExecutor& executor,
                                              uint8_t program_point_count)
{
    tecomm::StatusReportPayload status{};
    status.machine_state =
        static_cast<tecomm::ProtocolMachineState>(static_cast<uint8_t>(machine.machineState()));
    status.motion_state =
        static_cast<tecomm::ProtocolMotionState>(static_cast<uint8_t>(machine.motionState()));
    status.process_state =
        static_cast<tecomm::ProtocolProcessState>(static_cast<uint8_t>(executor.processState()));
    status.x = machine.x();
    status.y = machine.y();
    status.z = machine.z();

    const HostInputs& inputs = machine.hostInputs();
    status.fx = inputs.force.fx;
    status.fy = inputs.force.fy;
    status.fz = inputs.force.fz;

    uint8_t limits = 0;
    if (inputs.limitXMin)
    {
        limits |= static_cast<uint8_t>(1u << tecomm::kLimitBitXMin);
    }
    if (inputs.limitXMax)
    {
        limits |= static_cast<uint8_t>(1u << tecomm::kLimitBitXMax);
    }
    if (inputs.limitYMin)
    {
        limits |= static_cast<uint8_t>(1u << tecomm::kLimitBitYMin);
    }
    if (inputs.limitYMax)
    {
        limits |= static_cast<uint8_t>(1u << tecomm::kLimitBitYMax);
    }
    if (inputs.limitZMin)
    {
        limits |= static_cast<uint8_t>(1u << tecomm::kLimitBitZMin);
    }
    if (inputs.limitZMax)
    {
        limits |= static_cast<uint8_t>(1u << tecomm::kLimitBitZMax);
    }
    status.limits = limits;
    status.e_stop = inputs.e_stop ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);

    const HostOutputs outputs = machine.hostOutputs();
    status.spreader_extend = outputs.spreaderExtend ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
    status.spreader_extended = outputs.spreaderExtended ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
    status.fixture_extend = outputs.fixtureExtend ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
    status.fixture_extended = outputs.fixtureExtended ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);

    status.program_point_count = program_point_count;

    status.homing_state = static_cast<uint8_t>(machine.homingState());
    status.blocked_directions = machine.blockedDirections();

    uint8_t flags = 0;
    if (machine.isHomed())
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagIsHomed);
    }
    if (machine.limitWarningActive())
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagLimitWarning);
    }
    if (machine.communicationWatchdogEnabled())
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagCommWatchdog);
    }
    if (machine.forceSampleAge() < machine.machineConfig().force.sample_timeout_s)
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagForceFresh);
    }
    if (machine.calibrationSet().plate_origin_valid)
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagPlateOriginValid);
    }
    if (machine.calibrationSet().probe_center_valid)
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagProbeCenterValid);
    }
    if (machine.machineCoordinateSystemValid())
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagMcsValid);
    }
    if (machine.forceTareActive())
    {
        flags |= static_cast<uint8_t>(1u << tecomm::kStatusFlagTareActive);
    }
    status.flags = flags;
    status.procedure_phase = static_cast<uint8_t>(executor.procedurePhase());
    status.fault_reason = static_cast<uint8_t>(machine.lastFaultReason());
    status.spreader_state = static_cast<uint8_t>(machine.spreaderState());

    if (executor.processState() == ProcessState::Running)
    {
        const std::size_t idx = executor.currentIndex();
        status.current_point_index =
            (idx < 255) ? static_cast<uint8_t>(idx) : tecomm::kNoCurrentPointIndex;
    }
    else
    {
        status.current_point_index = tecomm::kNoCurrentPointIndex;
    }

    return status;
}

} // namespace simbridge
