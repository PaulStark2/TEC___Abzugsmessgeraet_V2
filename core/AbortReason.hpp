#pragma once

#include <cstdint>

// Stable abort codes for soft procedure aborts (not MachineState::Fault).
// Wire: StatusReport.abort_reason as uint16 LE. GUI maps codes to DE title/detail.
enum class AbortReason : uint16_t
{
    None = 0,
    Unspecified = 1,
    ForceTareBusy = 2,
    PointApproachXyMiss = 3,
    PointApproachSafeZ = 4,
    ProbeApproachSafeZLeg0 = 5,
    ProbeApproachXyMiss = 6,
    ProbeApproachSafeZLeg1 = 7,
    ProbeBoreSearchFailed = 8,
    ProbeCalibrationError = 9,
    PlateBoreTimeout = 10,
    PlateBoreWallNotFound = 11,
    PlateBoreFloorNotFound = 12,
    PlateBoreSearchFailed = 13,
    PlateBoreStartFailed = 14,
    CalibApproachSafeZLeg0 = 15,
    CalibApproachXyMiss = 16,
    CalibApproachSafeZLeg1 = 17,
    PreclampStartFailed = 18,
    PreclampTimeout = 19,
    PreclampWallNotFound = 20,
    PreclampSearchFailed = 21,
    ClampAfterTestStage = 22,
    InsertInitialNoSeatForce = 23,
    TestOrAgeNoKraftfreiExtract = 24,
    TestOrAgeNoZugOnExtract = 25,
    TestOrAgeExtractAfterZug = 26,
    TestOrAgeInsertNoTarget = 27,
    TestOrAgeExtractNoKraftfrei = 28,
    UnclampAfterTestReseatFailed = 29,
    UnclampAfterTestSafeZ = 30,
    ProbeRemeasureOffsetOnlyStart = 31,
    ProbeRemeasureOffsetOnlyError = 32,
    ProbeRemeasureNotAtSafeZ = 33,
    ProbeRemeasureXyMiss = 34,
    MoveSafePoseFailed = 35,
    NextPointFailed = 36,
    SoftStop = 37
};

// English stderr / log line (no heap).
inline const char* abortReasonMessage(AbortReason reason)
{
    switch (reason)
    {
    case AbortReason::None:
        return "none";
    case AbortReason::Unspecified:
        return "unspecified";
    case AbortReason::ForceTareBusy:
        return "force tare busy";
    case AbortReason::PointApproachXyMiss:
        return "PointApproach: XY not at programmed target before Z descent";
    case AbortReason::PointApproachSafeZ:
        return "PointApproach: Safe-Z lost";
    case AbortReason::ProbeApproachSafeZLeg0:
        return "ProbeApproach leg0: Raise Safe-Z did not reach (soft-limit or blocked)";
    case AbortReason::ProbeApproachXyMiss:
        return "ProbeApproach: XY not at probe center - tool_offset before Z descent";
    case AbortReason::ProbeApproachSafeZLeg1:
        return "ProbeApproach leg1: left Safe-Z before coarse Z";
    case AbortReason::ProbeBoreSearchFailed:
        return "probe bore search failed";
    case AbortReason::ProbeCalibrationError:
        return "probe calibration error";
    case AbortReason::PlateBoreTimeout:
        return "plate bore search timeout";
    case AbortReason::PlateBoreWallNotFound:
        return "plate bore wall not found";
    case AbortReason::PlateBoreFloorNotFound:
        return "plate bore floor not found";
    case AbortReason::PlateBoreSearchFailed:
        return "plate bore search failed";
    case AbortReason::PlateBoreStartFailed:
        return "plate bore start failed after tare";
    case AbortReason::CalibApproachSafeZLeg0:
        return "CalibApproach leg0: Raise Safe-Z-Calib did not reach";
    case AbortReason::CalibApproachXyMiss:
        return "CalibApproach: XY not at target before Z descent";
    case AbortReason::CalibApproachSafeZLeg1:
        return "CalibApproach leg1: left Safe-Z-Calib before coarse Z";
    case AbortReason::PreclampStartFailed:
        return "preclamp bore search start failed";
    case AbortReason::PreclampTimeout:
        return "preclamp bore search timeout";
    case AbortReason::PreclampWallNotFound:
        return "preclamp wall not found";
    case AbortReason::PreclampSearchFailed:
        return "preclamp bore search failed";
    case AbortReason::ClampAfterTestStage:
        return "Clamp after test stage";
    case AbortReason::InsertInitialNoSeatForce:
        return "InsertInitial: no seat force";
    case AbortReason::TestOrAgeNoKraftfreiExtract:
        return "TestOrAge: no kraftfrei on extract";
    case AbortReason::TestOrAgeNoZugOnExtract:
        return "TestOrAge: no Zug/kraftfrei on extract";
    case AbortReason::TestOrAgeExtractAfterZug:
        return "TestOrAge: extract without kraftfrei after Zug";
    case AbortReason::TestOrAgeInsertNoTarget:
        return "TestOrAge: insert without target force";
    case AbortReason::TestOrAgeExtractNoKraftfrei:
        return "TestOrAge: extract without kraftfrei";
    case AbortReason::UnclampAfterTestReseatFailed:
        return "Unclamp AfterTest: re-seat failed";
    case AbortReason::UnclampAfterTestSafeZ:
        return "Unclamp AfterTest: Safe-Z not reached";
    case AbortReason::ProbeRemeasureOffsetOnlyStart:
        return "ProbeRemeasure: OffsetOnly start failed";
    case AbortReason::ProbeRemeasureOffsetOnlyError:
        return "ProbeRemeasure OffsetOnly error";
    case AbortReason::ProbeRemeasureNotAtSafeZ:
        return "ProbeRemeasure: not at Safe-Z";
    case AbortReason::ProbeRemeasureXyMiss:
        return "ProbeRemeasure: XY miss";
    case AbortReason::MoveSafePoseFailed:
        return "MoveSafePose failed";
    case AbortReason::NextPointFailed:
        return "NextPoint failed";
    case AbortReason::SoftStop:
        return "soft stop";
    default:
        return "unspecified";
    }
}
