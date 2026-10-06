#pragma once

#include "HostInputs.hpp"
#include "Machine.hpp"

#include <cstdint>

class FirmwareHost
{
public:
    FirmwareHost();

    void tickOneMillisecond(const HostInputs& inputs);

    Machine& machine();
    const Machine& machine() const;

    std::uint32_t tickCount() const;
    std::uint32_t nextForceSequence() const;

private:
    Machine machine_;
    std::uint32_t tick_count_;
    std::uint32_t force_sequence_;
};
