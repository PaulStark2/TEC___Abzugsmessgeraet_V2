#pragma once

#include "Decoder.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"

#include "Machine.hpp"
#include "TestExecutor.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace simbridge
{

// One encoded M12 wire frame ready for TCP send.
struct OutboundFrame
{
    uint8_t data[tecomm::kMaxFrameSize];
    std::size_t length = 0;
};

// Maps decoded M12 command frames to Core APIs and produces outbound responses.
class CommandDispatcher
{
public:
    CommandDispatcher(Machine& machine, TestExecutor& executor);

    // Dispatch one inbound frame. Appends zero or more outbound frames.
    void dispatch(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                  std::vector<OutboundFrame>& out);

    uint8_t programPointCount() const;
    void setProgramPointCount(uint8_t count);

private:
    void appendAck(uint8_t ref_message_id, tecomm::AckResultCode result, std::vector<OutboundFrame>& out);
    void appendNack(uint8_t ref_message_id, tecomm::NackErrorCode error, uint8_t detail,
                    std::vector<OutboundFrame>& out);
    void appendStatusReport(std::vector<OutboundFrame>& out);
    void appendProgramSummary(const tecomm::LoadProgramPayload& loaded,
                              std::vector<OutboundFrame>& out);

    bool isFaultGuarded(uint8_t message_id) const;

    Machine& machine_;
    TestExecutor& executor_;
    uint8_t program_point_count_ = 0;
};

} // namespace simbridge
