#include "FirmwareHost.hpp"

FirmwareHost::FirmwareHost()
    : tick_count_(0)
    , force_sequence_(0)
{
}

void FirmwareHost::tickOneMillisecond(const HostInputs& inputs)
{
    machine_.setHostInputs(inputs);
    machine_.tick(0.001f);
    ++tick_count_;
    ++force_sequence_;
}

Machine& FirmwareHost::machine()
{
    return machine_;
}

const Machine& FirmwareHost::machine() const
{
    return machine_;
}

std::uint32_t FirmwareHost::tickCount() const
{
    return tick_count_;
}

std::uint32_t FirmwareHost::nextForceSequence() const
{
    return force_sequence_ + 1u;
}
