#pragma once

#include "Decoder.hpp"
#include "ProtocolSpec.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace simbridge
{

// TCP byte-stream reassembly for M12 frames (M13 transport layer).
class FrameStreamBuffer
{
public:
    // Append newly received TCP bytes.
    void append(const uint8_t* data, std::size_t length);

    // Try to extract one frame. Returns true when a frame is ready for the bridge.
    // decode_result is Ok, UnknownMessageId, or InvalidPayload for bridge dispatch;
    // stream-level errors are handled internally (resync, no output).
    bool tryPopFrame(tecomm::DecodedFrame& out, tecomm::DecodeResult& decode_result);

    // Clear all buffered bytes (e.g. on client disconnect).
    void clear();

    // True when buffer exceeded the safety limit and was cleared.
    bool overflowed() const;

    // Current buffered byte count.
    std::size_t size() const;

private:
    // Drop bytes from the front of the internal buffer.
    void discardFront(std::size_t count);

    // Resync after corrupt data: search for magic TE from start_index.
    void resyncFrom(std::size_t start_index);

    // Compute full frame size from a header at buffer_[0] (needs >= 6 bytes).
    std::size_t frameSizeFromHeader() const;

    std::vector<uint8_t> buffer_;
    bool overflowed_ = false;

    static constexpr std::size_t kMaxBufferSize = 4 * tecomm::kMaxFrameSize;
};

} // namespace simbridge
