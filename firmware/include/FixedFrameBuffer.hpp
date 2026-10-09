#pragma once

#include "Decoder.hpp"
#include "ProtocolSpec.hpp"

#include <cstddef>
#include <cstdint>

// MCU-safe M12 byte-stream reassembly (no heap). Same rules as simbridge::FrameStreamBuffer.
class FixedFrameBuffer
{
public:
    void append(const std::uint8_t* data, std::size_t length);
    bool tryPopFrame(tecomm::DecodedFrame& out, tecomm::DecodeResult& decode_result);
    void clear();
    bool overflowed() const;
    std::size_t size() const;

private:
    void discardFront(std::size_t count);
    void resyncFrom(std::size_t start_index);
    std::size_t frameSizeFromHeader() const;

    static constexpr std::size_t kCap = 4 * tecomm::kMaxFrameSize;
    std::uint8_t buffer_[kCap]{};
    std::size_t length_ = 0;
    bool overflowed_ = false;
};
