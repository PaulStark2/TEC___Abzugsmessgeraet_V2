#include "Decoder.hpp"

#include "Crc16.hpp"
#include "PayloadCodec.hpp"
#include "WireUtil.hpp"

namespace tecomm
{

namespace
{

bool isEmptyCommandId(uint8_t message_id)
{
    switch (static_cast<MessageId>(message_id))
    {
    case MessageId::CmdNop:
    case MessageId::CmdStop:
    case MessageId::CmdPause:
    case MessageId::CmdResume:
    case MessageId::CmdStart:
    case MessageId::CmdClearFault:
    case MessageId::CmdHome:
    case MessageId::CmdGetMachineConfig:
    case MessageId::CmdStartProbeCalib:
    case MessageId::CmdRequestStatus:
        return true;
    default:
        return false;
    }
}

bool validateBoolByte(uint8_t value)
{
    return value == 0 || value == 1;
}

} // namespace

bool isKnownMessageId(uint8_t message_id)
{
    switch (static_cast<MessageId>(message_id))
    {
    case MessageId::CmdNop:
    case MessageId::CmdStop:
    case MessageId::CmdPause:
    case MessageId::CmdResume:
    case MessageId::CmdStart:
    case MessageId::CmdClearFault:
    case MessageId::CmdHome:
    case MessageId::CmdMoveTo:
    case MessageId::CmdLoadProgram:
    case MessageId::CmdSetSpreader:
    case MessageId::CmdSetFixture:
    case MessageId::CmdSetMachineConfig:
    case MessageId::CmdGetMachineConfig:
    case MessageId::CmdSetTestConfiguration:
    case MessageId::CmdSetPlateOrigin:
    case MessageId::CmdStartProbeCalib:
    case MessageId::CmdRequestStatus:
    case MessageId::RspAck:
    case MessageId::RspNack:
    case MessageId::MsgStatusReport:
    case MessageId::MsgProgramSummary:
    case MessageId::MsgForcePathChunk:
    case MessageId::MsgMachineConfig:
        return true;
    default:
        return false;
    }
}

bool validatePayloadForMessage(uint8_t message_id, const uint8_t* payload, std::size_t payload_length)
{
    if (!isKnownMessageId(message_id))
    {
        return false;
    }

    if (isEmptyCommandId(message_id))
    {
        return payload_length == 0;
    }

    switch (static_cast<MessageId>(message_id))
    {
    case MessageId::CmdMoveTo:
        if (payload_length != kMoveToPayloadWireSize)
        {
            return false;
        }
        {
            std::size_t offset = 0;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!readF32LE(payload, payload_length, offset, x, true) ||
                !readF32LE(payload, payload_length, offset, y, true) ||
                !readF32LE(payload, payload_length, offset, z, true) || offset != payload_length)
            {
                return false;
            }
        }
        return true;

    case MessageId::CmdSetSpreader:
    case MessageId::CmdSetFixture:
        if (payload_length != kBoolPayloadWireSize)
        {
            return false;
        }
        return validateBoolByte(payload[0]);

    case MessageId::CmdLoadProgram:
    {
        if (payload_length < 1)
        {
            return false;
        }
        const uint8_t point_count = payload[0];
        if (point_count > kLoadProgramMaxPointCount)
        {
            return false;
        }
        const std::size_t expected = 1 + static_cast<std::size_t>(point_count) * kMoveToPayloadWireSize;
        if (payload_length != expected)
        {
            return false;
        }
        std::size_t offset = 1;
        for (uint8_t i = 0; i < point_count; ++i)
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!readF32LE(payload, payload_length, offset, x, true) ||
                !readF32LE(payload, payload_length, offset, y, true) ||
                !readF32LE(payload, payload_length, offset, z, true))
            {
                return false;
            }
        }
        return offset == payload_length;
    }

    case MessageId::RspAck:
        return payload_length == kAckPayloadWireSize;

    case MessageId::RspNack:
        return payload_length == kNackPayloadWireSize;

    case MessageId::MsgStatusReport:
        if (payload_length != kStatusReportPayloadWireSize)
        {
            return false;
        }
        {
            std::size_t offset = 3;
            float values[6] = {};
            for (int i = 0; i < 6; ++i)
            {
                if (!readF32LE(payload, payload_length, offset, values[i], true))
                {
                    return false;
                }
            }
            if (offset != 27)
            {
                return false;
            }
            if (!validateBoolByte(payload[28]))
            {
                return false;
            }
            if (!validateBoolByte(payload[29]) || !validateBoolByte(payload[30]) ||
                !validateBoolByte(payload[31]) || !validateBoolByte(payload[32]))
            {
                return false;
            }
            return true;
        }

    case MessageId::MsgProgramSummary:
        if (payload_length != kProgramSummaryPayloadWireSize)
        {
            return false;
        }
        {
            std::size_t offset = 2;
            float values[6] = {};
            for (int i = 0; i < 6; ++i)
            {
                if (!readF32LE(payload, payload_length, offset, values[i], true))
                {
                    return false;
                }
            }
            return offset == payload_length;
        }

    case MessageId::CmdSetMachineConfig:
    case MessageId::MsgMachineConfig:
        if (payload_length != kMachineConfigPayloadWireSize)
        {
            return false;
        }
        {
            MachineConfigPayload parsed{};
            return decodeMachineConfigPayload(payload, payload_length, parsed);
        }

    case MessageId::CmdSetTestConfiguration:
        if (payload_length != kTestConfigurationPayloadWireSize)
        {
            return false;
        }
        {
            TestConfigurationPayload parsed{};
            return decodeTestConfigurationPayload(payload, payload_length, parsed);
        }

    case MessageId::CmdSetPlateOrigin:
        if (payload_length != kPlateOriginPayloadWireSize)
        {
            return false;
        }
        {
            MoveToPayload parsed{};
            return decodeMoveToPayload(payload, payload_length, parsed);
        }

    case MessageId::MsgForcePathChunk:
    {
        ForcePathChunkPayload parsed{};
        return decodeForcePathChunkPayload(payload, payload_length, parsed);
    }

    default:
        return false;
    }
}

DecodeResult decodeFrame(const uint8_t* data, std::size_t data_length, DecodedFrame& out,
                         std::size_t& bytes_consumed)
{
    bytes_consumed = 0;

    if (data_length < kMinFrameSize)
    {
        return DecodeResult::NeedMoreData;
    }

    if (data[0] != kMagic0 || data[1] != kMagic1)
    {
        return DecodeResult::InvalidFrame;
    }

    const uint8_t version = data[2];
    if (version != kProtocolVersion)
    {
        return DecodeResult::UnsupportedVersion;
    }

    const uint8_t message_id = data[3];
    const uint16_t payload_length =
        static_cast<uint16_t>(data[4]) | static_cast<uint16_t>(static_cast<uint16_t>(data[5]) << 8);

    if (payload_length > kMaxPayloadSize)
    {
        return DecodeResult::InvalidLength;
    }

    const std::size_t frame_size = 6 + static_cast<std::size_t>(payload_length) + 2;
    if (data_length < frame_size)
    {
        return DecodeResult::NeedMoreData;
    }

    const uint16_t expected_crc = crc16CcittFalse(data, 6 + payload_length);
    const uint16_t received_crc =
        static_cast<uint16_t>(data[6 + payload_length + 0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[6 + payload_length + 1]) << 8);
    if (expected_crc != received_crc)
    {
        return DecodeResult::CrcError;
    }

    out.protocol_version = version;
    out.message_id = message_id;
    out.payload = (payload_length > 0) ? (data + 6) : nullptr;
    out.payload_length = payload_length;
    bytes_consumed = frame_size;

    if (!isKnownMessageId(message_id))
    {
        return DecodeResult::UnknownMessageId;
    }

    if (!validatePayloadForMessage(message_id, out.payload, out.payload_length))
    {
        return DecodeResult::InvalidPayload;
    }

    return DecodeResult::Ok;
}

} // namespace tecomm
