#pragma once

#include "CommandDispatcher.hpp"
#include "FrameStreamBuffer.hpp"
#include "SimHostInputProvider.hpp"

#include "Machine.hpp"
#include "TestExecutor.hpp"

#include <vector>

namespace simbridge
{

// M13 facade: command dispatch, status encoding, program metadata (no TCP).
class SimProtocolBridge
{
public:
    SimProtocolBridge();

    Machine& machine();
    const Machine& machine() const;
    TestExecutor& executor();
    const TestExecutor& executor() const;
    SimHostInputProvider& hostInputProvider();
    CommandDispatcher& dispatcher();

    // Process one decoded inbound frame; append outbound wire frames.
    void handleFrame(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                     std::vector<OutboundFrame>& out);

    // Encode current Core status as one outbound frame (20 Hz push / hello on connect).
    void appendStatusPush(std::vector<OutboundFrame>& out);

    // Apply simulated host inputs before each tick.
    void applyHostInputs();

    // Run one simulation tick (executor or machine depending on process state).
    void tick(float dt);

private:
    Machine machine_;
    TestExecutor executor_;
    SimHostInputProvider host_inputs_;
    CommandDispatcher dispatcher_;
};

} // namespace simbridge
