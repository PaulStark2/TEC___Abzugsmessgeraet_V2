#include "SimProtocolBridge.hpp"

#include "ForcePathRecorder.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "ForceSensorModel.hpp"
#include "ProcessForceModel.hpp"
#include "StatusSnapshot.hpp"
#include "Types.hpp"

namespace
{

bool phaseNeedsProcessForce(ProcedurePhase phase)
{
    return phase == ProcedurePhase::InsertInitial || phase == ProcedurePhase::ProbeRemeasure ||
           phase == ProcedurePhase::TestOrAge;
}

} // namespace

namespace simbridge
{

SimProtocolBridge::SimProtocolBridge()
    : executor_(machine_)
    , dispatcher_(machine_, executor_, &force_calib_)
{
    // Simulation contract:
    // ProcedureController reads hostInputs after advancing motion for this tick.
    // Therefore we refresh hostInputs here via a hook, so forces/trigger events
    // match the machine pose at the time the procedure logic evaluates them.
    machine_.setHostInputUpdateCallback(
        [this](Machine&) { this->applyHostInputs(); });
}

Machine& SimProtocolBridge::machine()
{
    return machine_;
}

const Machine& SimProtocolBridge::machine() const
{
    return machine_;
}

TestExecutor& SimProtocolBridge::executor()
{
    return executor_;
}

const TestExecutor& SimProtocolBridge::executor() const
{
    return executor_;
}

simplant::SimPlant& SimProtocolBridge::plant()
{
    return plant_;
}

const simplant::SimPlant& SimProtocolBridge::plant() const
{
    return plant_;
}

CommandDispatcher& SimProtocolBridge::dispatcher()
{
    return dispatcher_;
}

void SimProtocolBridge::handleFrame(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                                    std::vector<OutboundFrame>& out)
{
    dispatcher_.dispatch(frame, decode_result, out);
}

void SimProtocolBridge::appendStatusPush(std::vector<OutboundFrame>& out)
{
    const tecomm::StatusReportPayload status =
        buildStatusReport(machine_, executor_, dispatcher_.programPointCount());

    OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport), status, frame.data, sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }

    OutboundFrame adc_frame{};
    const tecomm::EncodeResult adc_encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::MsgRawAdc), force_calib_.raw(), adc_frame.data, sizeof(adc_frame.data));
    if (adc_encoded.ok)
    {
        adc_frame.length = adc_encoded.bytes_written;
        out.push_back(adc_frame);
    }

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
        OutboundFrame bore_frame{};
        const tecomm::EncodeResult bore_encoded = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::MsgBoreCenter), payload, bore_frame.data,
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
        OutboundFrame anomaly_frame{};
        const tecomm::EncodeResult anomaly_encoded = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::MsgProcedureAnomaly), payload, anomaly_frame.data,
            sizeof(anomaly_frame.data));
        if (anomaly_encoded.ok)
        {
            anomaly_frame.length = anomaly_encoded.bytes_written;
            out.push_back(anomaly_frame);
            procedure.consumeAnomalyReport();
        }
    }

}

void SimProtocolBridge::appendForcePathChunks(std::vector<OutboundFrame>& out)
{
    // Drain until empty or a soft frame budget so a slow host cannot stall the tick
    // while a full ring (256) would otherwise need eight 32-sample chunks.
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
        chunk.point_index = static_cast<uint8_t>(executor_.currentIndex());
        chunk.iteration = samples[0].iteration;
        chunk.sequence = recorder.sequence();
        chunk.sample_count = static_cast<uint8_t>(n);
        chunk.dropped = recorder.dropped();
        chunk.leg = samples[0].leg;
        for (std::size_t i = 0; i < n; ++i)
        {
            chunk.z[i] = samples[i].z;
            chunk.fz[i] = samples[i].fz;
        }

        OutboundFrame frame{};
        const tecomm::EncodeResult encoded = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::MsgForcePathChunk), chunk, frame.data,
            sizeof(frame.data));
        if (!encoded.ok)
        {
            return;
        }
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }
}

void SimProtocolBridge::applyHostInputs()
{
    // Probe-disc geometry sync removed with Messtaster measuring-head params;
    // plant contact is pulse-only. Disc center remains set via plant commands.

    HostInputs inputs = plant_.sample(machine_.rawX(), machine_.rawY(), machine_.rawZ());
    // SimPlant latches trigger XYZ in raw/plant. Core ProbeSearch/backoff treats
    // HostInputs.probeTrigger* as machine-frame (same as Machine::x/y/z).
    if (inputs.probeTriggerPoseValid)
    {
        inputs.probeTriggerX -= machine_.referenceOffsetX();
        inputs.probeTriggerY -= machine_.referenceOffsetY();
        inputs.probeTriggerZ -= machine_.referenceOffsetZ();
    }

    const ProcedurePhase phase = executor_.procedurePhase();
    const bool coupled = machine_.hostOutputs().spreaderExtended;
    if (executor_.processState() == ProcessState::Running && phaseNeedsProcessForce(phase) && coupled)
    {
        if (!process_force_latched_)
        {
            process_z_test_ = machine_.z();
            process_force_latched_ = true;
            process_force_unloaded_ = false;
        }

        const float target = machine_.testConfiguration().target_force_N;
        const float k = plant_.forceStiffnessNPerMm();
        // Keep Sim-UI injected Fx/Fy/Fz and add the process Z-spring on top.
        inputs.force.fz +=
            simplant::processFzZ(machine_.z(), process_z_test_, target, k, true, process_force_unloaded_);

        const float tol = machine_.machineConfig().force.force_tolerance_N;
        if (!process_force_unloaded_ &&
            simplant::processForceShouldMarkUnloaded(machine_.z(), process_z_test_, target, k, tol))
        {
            process_force_unloaded_ = true;
        }
    }
    else
    {
        if (!coupled || executor_.processState() != ProcessState::Running || !phaseNeedsProcessForce(phase))
        {
            process_force_latched_ = false;
            process_force_unloaded_ = false;
        }
        // Idle/jog: plant injected force (default 0) is the HostInputs vector.
    }

    // One Z curve on X/Y/Z. Tension stays negative; no second Fz flip.
    // Sim injects Core-frame forces (action convention, channels already mapped).
    // STM32 FirmwareHost swaps ADC X↔Y and negates Fx/Fy for the physical cell;
    // do not repeat that here or procedure tests would double-correct.
    const float dt = host_dt_ > 0.0f ? host_dt_ : 0.05f;
    const CountWindow::Mean mean = counts_.push(
        static_cast<float>(simplant::syntheticFzCounts(inputs.force.fx)),
        static_cast<float>(simplant::syntheticFzCounts(inputs.force.fy)),
        static_cast<float>(simplant::syntheticFzCounts(inputs.force.fz)), dt);
    force_calib_.observeRaw(roundAdcCount(mean.x), roundAdcCount(mean.y), roundAdcCount(mean.z), true);
    if (force_calib_.active())
    {
        inputs.force.fx = static_cast<float>(force_calib_.evaluateSensorNewton(static_cast<double>(mean.x)));
        inputs.force.fy = static_cast<float>(force_calib_.evaluateSensorNewton(static_cast<double>(mean.y)));
        inputs.force.fz = static_cast<float>(force_calib_.evaluateSensorNewton(static_cast<double>(mean.z)));
    }

    machine_.setHostInputs(inputs);
}

void SimProtocolBridge::tick(float dt)
{
    host_dt_ = dt > 0.0f ? dt : 0.05f;
    if (executor_.processState() == ProcessState::Running)
    {
        executor_.tick(dt);
    }
    else
    {
        // Manual moves / idle ticks: refresh hostInputs explicitly here.
        // During automatic procedures, ProcedureController invokes
        // Machine::invokeHostInputUpdateCallback() which calls applyHostInputs().
        applyHostInputs();
        machine_.tick(dt);
    }

    plant_.advance(dt);
    // Core emits the valve command only. Plant latches its own fixture pose.
    plant_.setFixtureValve(machine_.hostOutputs().fixtureExtend);
}

} // namespace simbridge
