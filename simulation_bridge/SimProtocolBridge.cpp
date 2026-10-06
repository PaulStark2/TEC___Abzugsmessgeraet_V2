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
    return phase == ProcedurePhase::InsertInitial || phase == ProcedurePhase::OptimizeInsert ||
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
}

void SimProtocolBridge::appendForcePathChunks(std::vector<OutboundFrame>& out)
{
    ForcePathRecorder& recorder = executor_.procedure().forcePath();
    tecomm::ForcePathChunkPayload chunk{};
    ForcePathRecorder::Sample samples[tecomm::kForcePathChunkMaxSamples];
    const std::size_t n = recorder.popSameIteration(samples, tecomm::kForcePathChunkMaxSamples);
    if (n == 0)
    {
        return;
    }
    chunk.point_index = static_cast<uint8_t>(executor_.currentIndex());
    chunk.iteration = samples[0].iteration;
    chunk.sequence = recorder.sequence();
    chunk.sample_count = static_cast<uint8_t>(n);
    chunk.dropped = recorder.dropped();
    for (std::size_t i = 0; i < n; ++i)
    {
        chunk.z[i] = samples[i].z;
        chunk.fz[i] = samples[i].fz;
    }

    OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::MsgForcePathChunk), chunk, frame.data, sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }
}

void SimProtocolBridge::applyHostInputs()
{
    // Disc center stays independently configurable. Collision geometry follows
    // MachineConfig so the spreader cylinder and disc size match the Core.
    simplant::ProbeDisc disc = plant_.probeDisc();
    const MachineConfig::Probe& probe = machine_.machineConfig().probe;
    disc.diameter = probe.measuring_head_diameter;
    disc.thickness = probe.measuring_head_thickness;
    disc.tool_diameter = probe.tool_diameter;
    disc.tool_cylindrical_height = probe.tool_cylindrical_height;
    plant_.setProbeDisc(disc);

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

        const float tol = machine_.machineConfig().phase2.force_tolerance_N;
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

    // One dose and one sign for every axis. The stored Z curve is applied to the
    // raw counts of X, Y and Z. Tension stays negative; there is no second flip.
    force_calib_.observeRaw(simplant::syntheticFzCounts(inputs.force.fx),
                            simplant::syntheticFzCounts(inputs.force.fy),
                            simplant::syntheticFzCounts(inputs.force.fz), true);
    if (force_calib_.active())
    {
        inputs.force.fx = static_cast<float>(force_calib_.evaluateSensorNewton(force_calib_.raw().x));
        inputs.force.fy = static_cast<float>(force_calib_.evaluateSensorNewton(force_calib_.raw().y));
        inputs.force.fz = static_cast<float>(force_calib_.evaluateSensorNewton(force_calib_.raw().z));
    }

    machine_.setHostInputs(inputs);
}

void SimProtocolBridge::tick(float dt)
{
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
