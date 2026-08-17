#include "SimProtocolBridge.hpp"

#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "StatusSnapshot.hpp"

#include "ForcePathRecorder.hpp"

namespace simbridge
{

SimProtocolBridge::SimProtocolBridge()
    : executor_(machine_)
    , dispatcher_(machine_, executor_)
{
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
}

void SimProtocolBridge::appendForcePathChunks(std::vector<OutboundFrame>& out)
{
    ForcePathRecorder& recorder = executor_.procedure().forcePath();
    tecomm::ForcePathChunkPayload chunk{};
    ForcePathRecorder::Sample samples[tecomm::kForcePathChunkMaxSamples];
    const std::size_t n = recorder.pop(samples, tecomm::kForcePathChunkMaxSamples);
    if (n == 0)
    {
        return;
    }
    chunk.point_index = static_cast<uint8_t>(executor_.currentIndex());
    chunk.iteration = static_cast<uint16_t>(executor_.procedure().testIteration());
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
    // Sensors are derived from the Core-reported pose; no safety decision here.
    machine_.setHostInputs(plant_.sample(machine_.x(), machine_.y(), machine_.z()));
}

void SimProtocolBridge::tick(float dt)
{
    if (executor_.processState() == ProcessState::Running)
    {
        executor_.tick(dt);
    }
    else
    {
        machine_.tick(dt);
    }

    plant_.advance(dt);
}

} // namespace simbridge
