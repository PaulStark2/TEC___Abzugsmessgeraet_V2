#include "FixedFrameBuffer.hpp"

void FixedFrameBuffer::append(const std::uint8_t* data, std::size_t length)
{
    if (data == nullptr || length == 0)
    {
        return;
    }
    if (length_ + length > kCap)
    {
        length_ = 0;
        overflowed_ = true;
        return;
    }
    for (std::size_t i = 0; i < length; ++i)
    {
        buffer_[length_++] = data[i];
    }
}

bool FixedFrameBuffer::tryPopFrame(tecomm::DecodedFrame& out, tecomm::DecodeResult& decode_result)
{
    for (;;)
    {
        if (length_ == 0)
        {
            return false;
        }
        if (buffer_[0] != tecomm::kMagic0 || buffer_[1] != tecomm::kMagic1)
        {
            resyncFrom(1);
            continue;
        }

        std::size_t consumed = 0;
        decode_result = tecomm::decodeFrame(buffer_, length_, out, consumed);

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

        if (decode_result == tecomm::DecodeResult::CrcError ||
            decode_result == tecomm::DecodeResult::UnsupportedVersion ||
            decode_result == tecomm::DecodeResult::InvalidLength)
        {
            if (length_ >= 6)
            {
                const std::size_t frame_size = frameSizeFromHeader();
                if (frame_size > 0 && frame_size <= length_)
                {
                    discardFront(frame_size);
                    continue;
                }
            }
            discardFront(1);
            continue;
        }

        resyncFrom(1);
    }
}

void FixedFrameBuffer::clear()
{
    length_ = 0;
    overflowed_ = false;
}

bool FixedFrameBuffer::overflowed() const
{
    return overflowed_;
}

std::size_t FixedFrameBuffer::size() const
{
    return length_;
}

void FixedFrameBuffer::discardFront(std::size_t count)
{
    if (count >= length_)
    {
        length_ = 0;
        return;
    }
    for (std::size_t i = 0; i < length_ - count; ++i)
    {
        buffer_[i] = buffer_[i + count];
    }
    length_ -= count;
}

void FixedFrameBuffer::resyncFrom(std::size_t start_index)
{
    for (std::size_t i = start_index; i + 1 < length_; ++i)
    {
        if (buffer_[i] == tecomm::kMagic0 && buffer_[i + 1] == tecomm::kMagic1)
        {
            discardFront(i);
            return;
        }
    }
    discardFront(1);
}

std::size_t FixedFrameBuffer::frameSizeFromHeader() const
{
    if (length_ < 6)
    {
        return 0;
    }
    const std::uint16_t payload_length =
        static_cast<std::uint16_t>(buffer_[4]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(buffer_[5]) << 8);
    if (payload_length > tecomm::kMaxPayloadSize)
    {
        return 0;
    }
    return 6 + static_cast<std::size_t>(payload_length) + 2;
}
