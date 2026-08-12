#include "SimProtocolBridge.hpp"

#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "StatusSnapshot.hpp"

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

SimHostInputProvider& SimProtocolBridge::hostInputProvider()
{
    return host_inputs_;
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

void SimProtocolBridge::applyHostInputs()
{
    machine_.setHostInputs(host_inputs_.build(machine_));
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
}

} // namespace simbridge
