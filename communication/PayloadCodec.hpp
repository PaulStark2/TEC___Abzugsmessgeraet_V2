#pragma once

#include "Encoder.hpp"
#include "Payloads.hpp"
#include "ProtocolTypes.hpp"

#include <cstddef>
#include <cstdint>

namespace tecomm
{

// Typed payload wire encode/decode (explicit layout, no struct aliasing).

std::size_t encodedPayloadSize(uint8_t message_id, const LoadProgramPayload* load_program);

bool encodeMoveToPayload(const MoveToPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeMoveToPayload(const uint8_t* data, std::size_t length, MoveToPayload& out);

bool encodeBoolPayload(const BoolPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeBoolPayload(const uint8_t* data, std::size_t length, BoolPayload& out);

bool encodeLoadProgramPayload(const LoadProgramPayload& payload, uint8_t* out, std::size_t capacity,
                              std::size_t& bytes_written);
bool decodeLoadProgramPayload(const uint8_t* data, std::size_t length, LoadProgramPayload& out);

bool encodeStatusReportPayload(const StatusReportPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeStatusReportPayload(const uint8_t* data, std::size_t length, StatusReportPayload& out);

bool encodeProgramSummaryPayload(const ProgramSummaryPayload& payload, uint8_t* out,
                                 std::size_t capacity);
bool decodeProgramSummaryPayload(const uint8_t* data, std::size_t length, ProgramSummaryPayload& out);

bool encodeAckPayload(const AckPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeAckPayload(const uint8_t* data, std::size_t length, AckPayload& out);

bool encodeNackPayload(const NackPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeNackPayload(const uint8_t* data, std::size_t length, NackPayload& out);

bool encodeMachineConfigPayload(const MachineConfigPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeMachineConfigPayload(const uint8_t* data, std::size_t length, MachineConfigPayload& out);

bool encodeTestConfigurationPayload(const TestConfigurationPayload& payload, uint8_t* out,
                                    std::size_t capacity);
bool decodeTestConfigurationPayload(const uint8_t* data, std::size_t length, TestConfigurationPayload& out);

bool encodeForcePathChunkPayload(const ForcePathChunkPayload& payload, uint8_t* out, std::size_t capacity,
                                 std::size_t& bytes_written);
bool decodeForcePathChunkPayload(const uint8_t* data, std::size_t length, ForcePathChunkPayload& out);

bool encodeSetForceCalibrationPayload(const SetForceCalibrationPayload& payload, uint8_t* out, std::size_t capacity,
                                      std::size_t& bytes_written);
bool decodeSetForceCalibrationPayload(const uint8_t* data, std::size_t length, SetForceCalibrationPayload& out);

bool encodeSelectForceCurvePayload(const SelectForceCurvePayload& payload, uint8_t* out, std::size_t capacity);
bool decodeSelectForceCurvePayload(const uint8_t* data, std::size_t length, SelectForceCurvePayload& out);

bool encodeRawAdcPayload(const RawAdcPayload& payload, uint8_t* out, std::size_t capacity);
bool decodeRawAdcPayload(const uint8_t* data, std::size_t length, RawAdcPayload& out);

bool encodeForceCalibrationResultPayload(const ForceCalibrationResultPayload& payload, uint8_t* out,
                                         std::size_t capacity, std::size_t& bytes_written);
bool decodeForceCalibrationResultPayload(const uint8_t* data, std::size_t length, ForceCalibrationResultPayload& out);

// Convenience: encode typed payload + frame header/CRC.
EncodeResult encodeMessage(uint8_t message_id, const MoveToPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const BoolPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const LoadProgramPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const StatusReportPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const ProgramSummaryPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const AckPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const NackPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const MachineConfigPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const TestConfigurationPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const ForcePathChunkPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const SetForceCalibrationPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const SelectForceCurvePayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const RawAdcPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeMessage(uint8_t message_id, const ForceCalibrationResultPayload& payload, uint8_t* out,
                           std::size_t out_capacity);
EncodeResult encodeEmptyMessage(uint8_t message_id, uint8_t* out, std::size_t out_capacity);

} // namespace tecomm
