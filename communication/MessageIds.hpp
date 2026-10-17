#pragma once

#include <cstdint>

namespace tecomm
{

// M12 message identifiers (v1).
enum class MessageId : uint8_t
{
    // Host -> Controller commands.
    CmdNop = 0x01,
    CmdStop = 0x10,
    CmdPause = 0x11,
    CmdResume = 0x12,
    CmdStart = 0x13,
    CmdClearFault = 0x14,
    CmdHome = 0x15,
    CmdSetManualSpeed = 0x16, // float32 mm/s — jog / manual moveTo velocity
    CmdMoveTo = 0x20,
    CmdLoadProgram = 0x30,
    CmdSetSpreader = 0x40,
    CmdSetFixture = 0x41,
    CmdSetMachineConfig = 0x50,
    CmdGetMachineConfig = 0x51,
    CmdSetTestConfiguration = 0x52,
    CmdSetPlateOrigin = 0x53,
    CmdStartProbeCalib = 0x54,
    CmdSetProbeCenter = 0x55,
    CmdDefineMcs = 0x56,
    CmdRestoreMcs = 0x57,
    CmdInvalidateHoming = 0x58,
    CmdSetForceCalibration = 0x59,
    CmdSelectForceCurve = 0x5A,
    CmdSetCollisionGuard = 0x5B,
    CmdForceTare = 0x5C, // empty — start a 20-sample rest tare while axes are still
    CmdRequestStatus = 0x7F,

    // Controller -> Host responses.
    RspAck = 0x80,
    RspNack = 0x81,

    // Controller -> Host telemetry.
    MsgStatusReport = 0xA0,
    MsgProgramSummary = 0xA1,
    MsgForcePathChunk = 0xA2,
    MsgMachineConfig = 0xA3,
    MsgRawAdc = 0xA4,
    MsgForceCalibration = 0xA5,
};

} // namespace tecomm
