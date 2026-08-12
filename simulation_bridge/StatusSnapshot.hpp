#pragma once

#include "Payloads.hpp"
#include "TestExecutor.hpp"

#include <cstdint>

namespace simbridge
{

// Build M12 StatusReportPayload from current Core state (read-only mapping).
tecomm::StatusReportPayload buildStatusReport(const Machine& machine, const TestExecutor& executor,
                                              uint8_t program_point_count);

} // namespace simbridge
