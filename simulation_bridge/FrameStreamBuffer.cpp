#include "FrameStreamBuffer.hpp"

#include <algorithm>

namespace simbridge
{

void FrameStreamBuffer::append(const uint8_t* data, std::size_t length)
{
    if (data == nullptr || length == 0)
    {
        return;
    }

    if (buffer_.size() + length > kMaxBufferSize)
    {
        buffer_.clear();
        overflowed_ = true;
        return;
    }

    buffer_.insert(buffer_.end(), data, data + length);
}

bool FrameStreamBuffer::tryPopFrame(tecomm::DecodedFrame& out, tecomm::DecodeResult& decode_result)
{
    for (;;)
    {
        if (buffer_.empty())
        {
            return false;
        }

        if (buffer_[0] != tecomm::kMagic0 || buffer_[1] != tecomm::kMagic1)
        {
            resyncFrom(1);
            continue;
        }

        std::size_t consumed = 0;
        decode_result = tecomm::decodeFrame(buffer_.data(), buffer_.size(), out, consumed);

        if (decode_result == tecomm::DecodeResult::NeedMoreData)
        {
            return false;
        }

        if (decode_result == tecomm::DecodeResult::Ok ||
            decode_result == tecomm::DecodeResult::UnknownMessageId ||
            decode_result == tecomm::DecodeResult::InvalidPayload)
        {
            discardFront(consumed);
            return true;
        }

        // Stream-level error: discard bad frame or resync; no response to client.
        if (decode_result == tecomm::DecodeResult::CrcError ||
            decode_result == tecomm::DecodeResult::UnsupportedVersion ||
            decode_result == tecomm::DecodeResult::InvalidLength)
        {
            if (buffer_.size() >= 6)
            {
                const std::size_t frame_size = frameSizeFromHeader();
                if (frame_size > 0 && frame_size <= buffer_.size())
                {
                    discardFront(frame_size);
                    continue;
                }
            }
            discardFront(1);
            continue;
        }

        // InvalidFrame or any other error at magic: resync.
        resyncFrom(1);
    }
}

void FrameStreamBuffer::clear()
{
    buffer_.clear();
    overflowed_ = false;
}

bool FrameStreamBuffer::overflowed() const
{
    return overflowed_;
}

std::size_t FrameStreamBuffer::size() const
{
    return buffer_.size();
}

void FrameStreamBuffer::discardFront(std::size_t count)
{
    if (count >= buffer_.size())
    {
        buffer_.clear();
        return;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count));
}

void FrameStreamBuffer::resyncFrom(std::size_t start_index)
{
    for (std::size_t i = start_index; i + 1 < buffer_.size(); ++i)
    {
        if (buffer_[i] == tecomm::kMagic0 && buffer_[i + 1] == tecomm::kMagic1)
        {
            discardFront(i);
            return;
        }
    }
    discardFront(1);
}

std::size_t FrameStreamBuffer::frameSizeFromHeader() const
{
    if (buffer_.size() < 6)
    {
        return 0;
    }

    const uint16_t payload_length =
        static_cast<uint16_t>(buffer_[4]) |
        static_cast<uint16_t>(static_cast<uint16_t>(buffer_[5]) << 8);

    if (payload_length > tecomm::kMaxPayloadSize)
    {
        return 0;
    }

    return 6 + static_cast<std::size_t>(payload_length) + 2;
}

} // namespace simbridge
