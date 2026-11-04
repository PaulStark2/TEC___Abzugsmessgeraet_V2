#pragma once

#include "BoreSearch.hpp"
#include "Machine.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

#include <cstdint>

// Non-blocking probe-bore calibration: Z-first BoreSearch then XY two-chord.
// ProcedureController talks only to this interface.
//
// StorePolicy:
//   Install    — FullCalib / Geometry: probeCenter = measured, toolOffset = 0
//   OffsetOnly — Procedure: keep probeCenter, toolOffset = storedCenter - measured
class ProbeCalibration
{
public:
    enum class State : uint8_t
    {
        Idle = 0,
        Running = 1,
        Complete = 2,
        Error = 3
    };

    enum class StorePolicy : uint8_t
    {
        Install = 0,
        OffsetOnly = 1
    };

    // hub: TCP pose at start (operator jog or ProbeApproach), above the bore.
    bool start(Machine& machine, const TestPoint& hub,
               StorePolicy policy = StorePolicy::Install);
    void tick(float dt, Machine& machine);
    void abort(Machine& machine);

    bool isComplete() const;
    bool isError() const;
    State state() const;
    TestPoint result() const;
    TestPoint toolOffset() const;
    StorePolicy storePolicy() const;
    BoreSearch::ErrorReason searchErrorReason() const;

private:
    bool finishAndStore(Machine& machine);
    void enterError();

    BoreSearch search_{};
    State state_ = State::Idle;
    StorePolicy policy_ = StorePolicy::Install;
    TestPoint hub_{};
    TestPoint result_{};
    TestPoint tool_offset_{};
};
