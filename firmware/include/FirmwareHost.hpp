#pragma once

#include "CommandDispatcher.hpp"
#include "ForceCalibHost.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"

#include <cstdint>
#include <vector>

class FirmwareHost
{
public:
    FirmwareHost();

    // Apply ForceCalib (if active) to ADC counts, then tick Core.
    void tickOneMillisecond(HostInputs inputs, std::uint16_t adc_x, std::uint16_t adc_y, std::uint16_t adc_z);

    void handleFrame(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                     std::vector<simbridge::OutboundFrame>& out);
    // MsgStatusReport + MsgRawAdc (same pair as simulation_protocol).
    void appendStatusPush(std::vector<simbridge::OutboundFrame>& out);

    Machine& machine();
    const Machine& machine() const;
    TestExecutor& executor();
    simbridge::CommandDispatcher& dispatcher();
    simbridge::ForceCalibHost& forceCalib();

    std::uint32_t tickCount() const;
    std::uint32_t nextForceSequence() const;
    // Last sequence observed by Core (bumped only on CmdNop while session is live).
    std::uint32_t heartbeatSequence() const;

private:
    Machine machine_;
    simbridge::ForceCalibHost force_calib_;
    TestExecutor executor_;
    simbridge::CommandDispatcher dispatcher_;
    std::uint32_t tick_count_;
    std::uint32_t force_sequence_;
    std::uint32_t heartbeat_sequence_;
};
