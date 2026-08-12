#pragma once

#include "MessageIds.hpp"
#include "ProtocolSpec.hpp"
#include "ProtocolTypes.hpp"

#include <cstddef>
#include <cstdint>

namespace tecomm
{

// Returns true for registered v1 message IDs.
bool isKnownMessageId(uint8_t message_id);

// Validate payload length/content for a known message ID after frame decode.
bool validatePayloadForMessage(uint8_t message_id, const uint8_t* payload, std::size_t payload_length);

// Decode one complete frame from a byte buffer (single-frame, no stream reassembly).
DecodeResult decodeFrame(const uint8_t* data, std::size_t data_length, DecodedFrame& out,
                         std::size_t& bytes_consumed);

} // namespace tecomm
