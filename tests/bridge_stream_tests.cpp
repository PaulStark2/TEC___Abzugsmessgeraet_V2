// Unit tests for M13 FrameStreamBuffer (TCP stream reassembly).
#include "FrameStreamBuffer.hpp"

#include "Encoder.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"

#include <cstring>
#include <vector>

static bool encodeMoveToFrame(float x, float y, float z, std::vector<uint8_t>& out)
{
    tecomm::MoveToPayload payload{x, y, z};
    uint8_t frame[tecomm::kMaxFrameSize];
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), payload, frame, sizeof(frame));
    if (!encoded.ok)
    {
        return false;
    }
    out.assign(frame, frame + encoded.bytes_written);
    return true;
}

static bool popOne(simbridge::FrameStreamBuffer& buffer, tecomm::DecodedFrame& frame,
                 tecomm::DecodeResult& result)
{
    return buffer.tryPopFrame(frame, result);
}

int main()
{
    // Partial frame: NeedMoreData until complete.
    {
        std::vector<uint8_t> full;
        if (!encodeMoveToFrame(1.0f, 2.0f, 3.0f, full))
        {
            return 1;
        }

        simbridge::FrameStreamBuffer buffer;
        tecomm::DecodedFrame frame{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;

        buffer.append(full.data(), full.size() / 2);
        if (popOne(buffer, frame, result))
        {
            return 2;
        }

        buffer.append(full.data() + full.size() / 2, full.size() - full.size() / 2);
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 3;
        }
        if (frame.message_id != static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo))
        {
            return 4;
        }
    }

    // Complete single frame in one append.
    {
        std::vector<uint8_t> full;
        if (!encodeMoveToFrame(4.0f, 5.0f, 6.0f, full))
        {
            return 10;
        }

        simbridge::FrameStreamBuffer buffer;
        buffer.append(full.data(), full.size());

        tecomm::DecodedFrame frame{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 11;
        }
        if (buffer.size() != 0)
        {
            return 12;
        }
    }

    // Two frames in one buffer.
    {
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
        if (!encodeMoveToFrame(1.0f, 0.0f, 0.0f, a) || !encodeMoveToFrame(2.0f, 0.0f, 0.0f, b))
        {
            return 20;
        }

        simbridge::FrameStreamBuffer buffer;
        buffer.append(a.data(), a.size());
        buffer.append(b.data(), b.size());

        tecomm::DecodedFrame frame{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;

        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 21;
        }
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 22;
        }
        if (buffer.size() != 0)
        {
            return 23;
        }
    }

    // Frame + partial next frame.
    {
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
        if (!encodeMoveToFrame(1.0f, 0.0f, 0.0f, a) || !encodeMoveToFrame(2.0f, 0.0f, 0.0f, b))
        {
            return 30;
        }

        simbridge::FrameStreamBuffer buffer;
        buffer.append(a.data(), a.size());
        buffer.append(b.data(), b.size() / 2);

        tecomm::DecodedFrame frame{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 31;
        }
        if (popOne(buffer, frame, result))
        {
            return 32;
        }

        buffer.append(b.data() + b.size() / 2, b.size() - b.size() / 2);
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 33;
        }
    }

    // CRC error resync: corrupt CRC then valid frame follows.
    {
        std::vector<uint8_t> bad;
        if (!encodeMoveToFrame(1.0f, 0.0f, 0.0f, bad))
        {
            return 40;
        }
        bad[bad.size() - 1] ^= 0xFF;

        std::vector<uint8_t> good;
        if (!encodeMoveToFrame(2.0f, 0.0f, 0.0f, good))
        {
            return 41;
        }

        simbridge::FrameStreamBuffer buffer;
        buffer.append(bad.data(), bad.size());
        buffer.append(good.data(), good.size());

        tecomm::DecodedFrame frame{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 42;
        }
        if (frame.message_id != static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo))
        {
            return 43;
        }
    }

    // Invalid leading bytes then resync to magic.
    {
        std::vector<uint8_t> good;
        if (!encodeMoveToFrame(3.0f, 0.0f, 0.0f, good))
        {
            return 50;
        }

        const uint8_t garbage[] = {0x00, 0x00, 0xFF, 0xFF};
        simbridge::FrameStreamBuffer buffer;
        buffer.append(garbage, sizeof(garbage));
        buffer.append(good.data(), good.size());

        tecomm::DecodedFrame frame{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;
        if (!popOne(buffer, frame, result) || result != tecomm::DecodeResult::Ok)
        {
            return 51;
        }
    }

    // Oversize buffer protection.
    {
        simbridge::FrameStreamBuffer buffer;
        std::vector<uint8_t> chunk(4 * tecomm::kMaxFrameSize + 1, 0x54);
        buffer.append(chunk.data(), chunk.size());
        if (!buffer.overflowed())
        {
            return 60;
        }
        if (buffer.size() != 0)
        {
            return 61;
        }
    }

    // Unknown message ID delivered to bridge layer.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult encoded = tecomm::encodeEmptyMessage(0xEE, frame, sizeof(frame));
        if (!encoded.ok)
        {
            return 70;
        }

        simbridge::FrameStreamBuffer buffer;
        buffer.append(frame, encoded.bytes_written);

        tecomm::DecodedFrame decoded{};
        tecomm::DecodeResult result = tecomm::DecodeResult::NeedMoreData;
        if (!popOne(buffer, decoded, result) || result != tecomm::DecodeResult::UnknownMessageId)
        {
            return 71;
        }
    }

    return 0;
}
