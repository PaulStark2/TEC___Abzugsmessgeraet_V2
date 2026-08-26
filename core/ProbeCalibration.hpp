#pragma once

#include "Machine.hpp"
#include "ProbeSearch.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

#include <cstdint>

// Non-blocking probe-disc calibration: Z, four XY contacts, circle fit.
// ProcedureController talks only to this interface.
//
// StorePolicy separates the two calibration cases:
//   Install    — FullCalib / Geometry: set probeCenter = measured, toolOffset = 0
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

    enum class Step : uint8_t
    {
        Idle = 0,
        MeasureZ = 1,
        MeasureNegY = 2,
        MeasurePosY = 3,
        MeasureNegX = 4,
        MeasurePosX = 5,
        FitCenter = 6,
        Complete = 7,
        Error = 8
    };

    enum class StorePolicy : uint8_t
    {
        Install = 0,
        OffsetOnly = 1
    };

    // search_reference: current TCP pose used as origin for ProbeSearch moves.
    // Must be provided by the caller so ProbeCalibration never assumes any
    // configured probe position. probe_offset_reference is NOT used for offset math.
    bool start(Machine& machine, const TestPoint& search_reference,
               StorePolicy policy = StorePolicy::Install);
    void tick(float dt, Machine& machine);
    void abort();

    bool isComplete() const;
    bool isError() const;
    State state() const;
    Step step() const;
    TestPoint result() const;
    TestPoint toolOffset() const;
    StorePolicy storePolicy() const;

private:
    ProbeSearch::Request makeRequest(const Machine& machine, ProbeSearchDir dir) const;
    bool startDirection(Machine& machine, ProbeSearchDir dir, Step next_step);
    bool finishAndStore(Machine& machine);
    void enterError();

    ProbeSearch search_{};
    State state_ = State::Idle;
    Step step_ = Step::Idle;
    StorePolicy policy_ = StorePolicy::Install;
    TestPoint reference_{};
    TestPoint result_{};
    TestPoint tool_offset_{};
    TestPoint contact_z_{};
    TestPoint contact_neg_y_{};
    TestPoint contact_pos_y_{};
    TestPoint contact_neg_x_{};
    TestPoint contact_pos_x_{};
};
