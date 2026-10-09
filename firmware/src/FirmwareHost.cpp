#include "FirmwareHost.hpp"

#include "MachineConfig.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "StatusSnapshot.hpp"
#include "Types.hpp"

FirmwareHost::FirmwareHost()
    : executor_(machine_)
    , dispatcher_(machine_, executor_, &force_calib_)
    , tick_count_(0)
    , force_sequence_(0)
    , heartbeat_sequence_(0)
{
    MachineConfig cfg{};
    ConfigValidationError err{};
    (void)machine_.trySetMachineConfig(cfg, err);
}

void FirmwareHost::tickOneMillisecond(HostInputs inputs, std::uint16_t adc_x, std::uint16_t adc_y,
                                      std::uint16_t adc_z)
{
    force_calib_.observeRaw(adc_x, adc_y, adc_z, true);

    // Same contract as SimProtocolBridge: one Z curve on all three ADC channels.
    // Without a committed curve, keep the provisional AdcScaler Newtons from HostIo.
    if (force_calib_.active())
    {
        inputs.force.fx = static_cast<float>(force_calib_.evaluateSensorNewton(adc_x));
        inputs.force.fy = static_cast<float>(force_calib_.evaluateSensorNewton(adc_y));
        inputs.force.fz = static_cast<float>(force_calib_.evaluateSensorNewton(adc_z));
    }

    machine_.setHostInputs(inputs);
    if (executor_.processState() == ProcessState::Running)
    {
        executor_.tick(0.001f);
    }
    else
    {
        machine_.tick(0.001f);
    }
    ++tick_count_;
    ++force_sequence_;
}

void FirmwareHost::handleFrame(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                               std::vector<simbridge::OutboundFrame>& out)
{
    // Phase 6: Core communication watchdog (Automatic only) advances solely on CmdNop.
    // Force samples stay on their own sequence — do not conflate the two.
    if (decode_result == tecomm::DecodeResult::Ok &&
        frame.message_id == static_cast<std::uint8_t>(tecomm::MessageId::CmdNop))
    {
        ++heartbeat_sequence_;
    }

    dispatcher_.dispatch(frame, decode_result, out);
}

void FirmwareHost::appendStatusPush(std::vector<simbridge::OutboundFrame>& out)
{
    const tecomm::StatusReportPayload status =
        simbridge::buildStatusReport(machine_, executor_, dispatcher_.programPointCount());

    simbridge::OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<std::uint8_t>(tecomm::MessageId::MsgStatusReport), status, frame.data,
        sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }

    simbridge::OutboundFrame adc_frame{};
    const tecomm::EncodeResult adc_encoded =
        tecomm::encodeMessage(static_cast<std::uint8_t>(tecomm::MessageId::MsgRawAdc), force_calib_.raw(),
                              adc_frame.data, sizeof(adc_frame.data));
    if (adc_encoded.ok)
    {
        adc_frame.length = adc_encoded.bytes_written;
        out.push_back(adc_frame);
    }
}

Machine& FirmwareHost::machine()
{
    return machine_;
}

const Machine& FirmwareHost::machine() const
{
    return machine_;
}

TestExecutor& FirmwareHost::executor()
{
    return executor_;
}

simbridge::CommandDispatcher& FirmwareHost::dispatcher()
{
    return dispatcher_;
}

simbridge::ForceCalibHost& FirmwareHost::forceCalib()
{
    return force_calib_;
}

std::uint32_t FirmwareHost::tickCount() const
{
    return tick_count_;
}

std::uint32_t FirmwareHost::nextForceSequence() const
{
    return force_sequence_ + 1u;
}

std::uint32_t FirmwareHost::heartbeatSequence() const
{
    return heartbeat_sequence_;
}
