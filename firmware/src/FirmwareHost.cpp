#include "FirmwareHost.hpp"

#include "AdcScaler.hpp"
#include "ForcePathRecorder.hpp"
#include "MachineConfig.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "ProtocolSpec.hpp"
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

void FirmwareHost::tickOneMillisecond(HostInputs inputs, float adc_x, float adc_y, float adc_z)
{
    const CountWindow::Mean mean = counts_.push(adc_x, adc_y, adc_z, 0.001f);

    // Mechanical dose axes are crossed: PA0 ↔ machine Y, PA1 ↔ machine X.
    // MsgRawAdc and HostInputs use machine-axis order after the swap.
    const float axis_x_counts = mean.y;
    const float axis_y_counts = mean.x;
    const float axis_z_counts = mean.z;
    force_calib_.observeRaw(roundAdcCount(axis_x_counts), roundAdcCount(axis_y_counts),
                            roundAdcCount(axis_z_counts), true);

    // Same Z curve on all three channels. The polynomial sees the unrounded mean.
    const float range_N = machine_.machineConfig().force.range_N;
    if (force_calib_.active())
    {
        inputs.force.fx =
            static_cast<float>(force_calib_.evaluateSensorNewton(static_cast<double>(axis_x_counts)));
        inputs.force.fy =
            static_cast<float>(force_calib_.evaluateSensorNewton(static_cast<double>(axis_y_counts)));
        inputs.force.fz =
            static_cast<float>(force_calib_.evaluateSensorNewton(static_cast<double>(axis_z_counts)));
    }
    else
    {
        inputs.force.fx = adc_scaler::countsToNewton(axis_x_counts, range_N);
        inputs.force.fy = adc_scaler::countsToNewton(axis_y_counts, range_N);
        inputs.force.fz = adc_scaler::countsToNewton(axis_z_counts, range_N);
    }

    // Cell reports reaction (−F when driving + into a stop). Core unload is
    // u = −sign(F); negate laterals once so +F aligns with +machine. Do not also
    // flip Core unload. Z unchanged (tension − / compression +).
    inputs.force.fx = -inputs.force.fx;
    inputs.force.fy = -inputs.force.fy;
#if TEC_INVERT_FY
    // Historical Zortrax-only extra Y flip; keep OFF on the SFU machine.
    inputs.force.fy = -inputs.force.fy;
#endif

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

    {
        ProcedureController& procedure = executor_.procedure();
        const ProcedureController::BoreReport& bore = procedure.boreReport();
        if (bore.fresh)
        {
            tecomm::BoreCenterPayload payload{};
            payload.cx = bore.cx;
            payload.cy = bore.cy;
            payload.cz = bore.cz;
            payload.radius = bore.radius;
            payload.residual = bore.residual;
            payload.fx = bore.fx;
            payload.fy = bore.fy;
            payload.anomaly = bore.anomaly ? 1 : 0;
            payload.confirm_pending = bore.confirm_pending ? 1 : 0;
            payload.valid = 1;
            payload.point_id = bore.point_id;
            for (int i = 0; i < 4; ++i)
            {
                payload.x[i] = bore.x[i];
                payload.y[i] = bore.y[i];
            }
            simbridge::OutboundFrame bore_frame{};
            const tecomm::EncodeResult bore_encoded = tecomm::encodeMessage(
                static_cast<std::uint8_t>(tecomm::MessageId::MsgBoreCenter), payload, bore_frame.data,
                sizeof(bore_frame.data));
            if (bore_encoded.ok)
            {
                bore_frame.length = bore_encoded.bytes_written;
                out.push_back(bore_frame);
                procedure.consumeBoreReport();
            }
        }

    const ProcedureController::AnomalyReport& anomaly = procedure.anomalyReport();
    if (anomaly.fresh)
    {
        tecomm::ProcedureAnomalyPayload payload{};
        payload.type = anomaly.type;
        payload.point_index = anomaly.point_index;
        payload.iteration = anomaly.iteration;
        payload.measured = anomaly.measured;
        payload.limit = anomaly.limit;
        payload.z = anomaly.z;
        payload.fx = anomaly.fx;
        payload.fy = anomaly.fy;
        payload.skip_applied = anomaly.skip_applied ? 1 : 0;
        payload.confirm_pending = anomaly.confirm_pending ? 1 : 0;
        simbridge::OutboundFrame anomaly_frame{};
        const tecomm::EncodeResult anomaly_encoded = tecomm::encodeMessage(
            static_cast<std::uint8_t>(tecomm::MessageId::MsgProcedureAnomaly), payload, anomaly_frame.data,
            sizeof(anomaly_frame.data));
        if (anomaly_encoded.ok)
        {
            anomaly_frame.length = anomaly_encoded.bytes_written;
            out.push_back(anomaly_frame);
            procedure.consumeAnomalyReport();
        }
    }

    }

    appendForcePathChunks(out);

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

void FirmwareHost::appendForcePathChunks(std::vector<simbridge::OutboundFrame>& out)
{
    constexpr std::size_t kMaxChunksPerPush = 8;
    ForcePathRecorder& recorder = executor_.procedure().forcePath();
    for (std::size_t chunk_i = 0; chunk_i < kMaxChunksPerPush; ++chunk_i)
    {
        tecomm::ForcePathChunkPayload chunk{};
        ForcePathRecorder::Sample samples[tecomm::kForcePathChunkMaxSamples];
        const std::size_t n =
            recorder.popSameIterationAndLeg(samples, tecomm::kForcePathChunkMaxSamples);
        if (n == 0)
        {
            return;
        }
        chunk.point_index = static_cast<std::uint8_t>(executor_.currentIndex());
        chunk.iteration = samples[0].iteration;
        chunk.sequence = recorder.sequence();
        chunk.sample_count = static_cast<std::uint8_t>(n);
        chunk.dropped = recorder.dropped();
        chunk.leg = samples[0].leg;
        for (std::size_t i = 0; i < n; ++i)
        {
            chunk.z[i] = samples[i].z;
            chunk.fz[i] = samples[i].fz;
        }

        simbridge::OutboundFrame frame{};
        const tecomm::EncodeResult encoded = tecomm::encodeMessage(
            static_cast<std::uint8_t>(tecomm::MessageId::MsgForcePathChunk), chunk, frame.data,
            sizeof(frame.data));
        if (!encoded.ok)
        {
            return;
        }
        frame.length = encoded.bytes_written;
        out.push_back(frame);
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
