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
    , prev_probe_triggered_(false)
    , probe_trigger_sequence_(0)
    , latched_probe_pose_valid_(false)
    , latched_probe_x_(0.0f)
    , latched_probe_y_(0.0f)
    , latched_probe_z_(0.0f)
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

    // Core ProbeSearch/Calibration ignore probeTriggered unless the host latches
    // machine-frame TCP + a new sequence (see HostInputs / Machine::setHostInputs).
    // SimPlant does this; firmware must too (1 kHz poll; EXTI can refine later).
    if (inputs.probeTriggered && !prev_probe_triggered_)
    {
        ++probe_trigger_sequence_;
        if (probe_trigger_sequence_ == 0u)
        {
            ++probe_trigger_sequence_;
        }
        latched_probe_pose_valid_ = true;
        latched_probe_x_ = machine_.x();
        latched_probe_y_ = machine_.y();
        latched_probe_z_ = machine_.z();
    }
    if (!inputs.probeTriggered)
    {
        latched_probe_pose_valid_ = false;
    }
    prev_probe_triggered_ = inputs.probeTriggered;

    if (latched_probe_pose_valid_)
    {
        inputs.probeTriggerPoseValid = true;
        inputs.probeTriggerSequence = probe_trigger_sequence_;
        inputs.probeTriggerX = latched_probe_x_;
        inputs.probeTriggerY = latched_probe_y_;
        inputs.probeTriggerZ = latched_probe_z_;
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

void FirmwareHost::appendStatusPush(std::vector<simbridge::OutboundFrame>& out, bool include_adc)
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

    if (!include_adc)
    {
        return;
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
