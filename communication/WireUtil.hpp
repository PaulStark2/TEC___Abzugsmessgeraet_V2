#pragma once

#include <cstddef>
#include <cstdint>

namespace tecomm
{

// Returns true when value is a finite float suitable for the wire.
bool isFiniteFloat(float value);

// Explicit little-endian wire primitives (no struct aliasing).
bool writeU8(uint8_t* buffer, std::size_t capacity, std::size_t& offset, uint8_t value);
bool writeU16LE(uint8_t* buffer, std::size_t capacity, std::size_t& offset, uint16_t value);
bool writeF32LE(uint8_t* buffer, std::size_t capacity, std::size_t& offset, float value);

bool readU8(const uint8_t* buffer, std::size_t length, std::size_t& offset, uint8_t& value_out);
bool readU16LE(const uint8_t* buffer, std::size_t length, std::size_t& offset, uint16_t& value_out);
bool readF32LE(const uint8_t* buffer, std::size_t length, std::size_t& offset, float& value_out,
               bool validate_finite);

} // namespace tecomm
