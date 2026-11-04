#pragma once

#include "BorePoint.hpp"
#include "Machine.hpp"
#include "TestPoint.hpp"

#include <cstdint>

// Two-chord rising-edge bore center search (PreClamp, plate teach, probe bore calib).
// Floor modes: none | after XY (plate) | before XY (probe Einmessen z_first).
class BoreSearch
{
public:
    enum class State : std::uint8_t
    {
        Idle = 0,
        FirstOut,
        FirstBack,
        ToMid,
        SecondOut,
        SecondBack,
        ToCenter,
        FloorDown,
        FloorLift,
        Complete,
        Error
    };

    enum class ErrorReason : std::uint8_t
    {
        None = 0,
        WallNotFound = 1,
        FloorNotFound = 2,
        Timeout = 3,
        Fault = 4,
        InvalidRequest = 5,
        Aborted = 6
    };

    enum class Motion : std::uint8_t
    {
        Command = 0,
        Move,
        Stop
    };

    struct Params
    {
        float wall_force_N = 0.5f;
        float floor_force_N = 0.5f;
        float xy_search_limit = 5.0f;
        float z_search_limit = 10.0f;
        float z_lift_mm = 2.0f;
        float timeout_s = 30.0f;
        std::uint8_t refine_passes = 0;
        bool do_floor = false; // after XY (plate teach)
        bool z_first = false; // before XY (probe bore); implies floor+lift then XY
    };

    struct Result
    {
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        float radius = 0.0f;
        bool anomaly = false;
        float fx = 0.0f;
        float fy = 0.0f;
        float fz = 0.0f;
        BorePoint points[4]{};
    };

    bool start(Machine& machine, const Params& params);
    void tick(float dt, Machine& machine);
    void abort();

    bool isComplete() const;
    bool isError() const;
    bool isBusy() const;
    State state() const;
    ErrorReason errorReason() const;
    const Result& result() const;
    const Params& params() const;

private:
    void resetXySearch(Machine& machine);
    void commandTravel(Machine& machine);
    void finishWall(Machine& machine);
    void enterError(ErrorReason reason);
    void finishCenter(Machine& machine);
    int seekSign(float component, float tolerance) const;
    static bool motionSettled(const Machine& machine);

    Params params_{};
    Result result_{};
    State state_ = State::Idle;
    ErrorReason error_ = ErrorReason::None;
    Motion motion_ = Motion::Command;

    bool axis_x_ = true;
    bool axis_chosen_ = false;
    bool second_chosen_ = false;
    int seek_sign_ = 1;
    bool edge_armed_ = false;
    std::uint8_t refine_left_ = 0;
    float elapsed_s_ = 0.0f;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float origin_z_ = 0.0f;
    float floor_z_ = 0.0f;
    BorePoint points_[4]{};
    int point_count_ = 0;
};
