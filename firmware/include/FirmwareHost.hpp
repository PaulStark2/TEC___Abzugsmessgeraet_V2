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
    // MsgStatusReport always; MsgRawAdc when include_adc (throttle on UART).
    void appendStatusPush(std::vector<simbridge::OutboundFrame>& out, bool include_adc = true);

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

    // Host-side probe edge latch (Core requires pose+sequence; level alone is ignored).
    bool prev_probe_triggered_;
    std::uint32_t probe_trigger_sequence_;
    bool latched_probe_pose_valid_;
    float latched_probe_x_;
    float latched_probe_y_;
    float latched_probe_z_;
};
