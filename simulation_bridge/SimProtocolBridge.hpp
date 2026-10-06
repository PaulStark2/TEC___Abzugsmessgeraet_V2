#pragma once

#include "CommandDispatcher.hpp"
#include "ForceCalibHost.hpp"
#include "FrameStreamBuffer.hpp"

#include "SimPlant.hpp"

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
    simplant::SimPlant& plant();
    const simplant::SimPlant& plant() const;
    CommandDispatcher& dispatcher();

    // Process one decoded inbound frame; append outbound wire frames.
    void handleFrame(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                     std::vector<OutboundFrame>& out);

    // Encode current Core status as one outbound frame (20 Hz push / hello on connect).
    void appendStatusPush(std::vector<OutboundFrame>& out);

    // Opportunistic force-path telemetry; never waits on the host.
    void appendForcePathChunks(std::vector<OutboundFrame>& out);

    // Apply simulated host inputs before each tick.
    void applyHostInputs();

    // Run one simulation tick (executor or machine depending on process state)
    // and advance the plant sensor timers with the same dt.
    void tick(float dt);

private:
    Machine machine_;
    TestExecutor executor_;
    simplant::SimPlant plant_;
    ForceCalibHost force_calib_;
    CommandDispatcher dispatcher_;
    float process_z_test_ = 0.0f;
    bool process_force_latched_ = false;
    bool process_force_unloaded_ = false;
};

} // namespace simbridge
