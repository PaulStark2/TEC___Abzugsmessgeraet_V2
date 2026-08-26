'use strict';

/**
 * M12 protocol constants mirrored from communication/ (C++ source of truth).
 * Do not invent new IDs or ordinals here.
 */

const MAGIC0 = 0x54;
const MAGIC1 = 0x45;
const PROTOCOL_VERSION = 1;

const MAX_PAYLOAD_SIZE = 512;
const MIN_FRAME_SIZE = 8;
const MAX_FRAME_SIZE = 520;

const MOVE_TO_PAYLOAD_WIRE_SIZE = 12;
const BOOL_PAYLOAD_WIRE_SIZE = 1;
const ACK_PAYLOAD_WIRE_SIZE = 2;
const NACK_PAYLOAD_WIRE_SIZE = 3;
const STATUS_REPORT_PAYLOAD_WIRE_SIZE = 41;
const PROGRAM_SUMMARY_PAYLOAD_WIRE_SIZE = 26;
const LOAD_PROGRAM_MAX_POINT_COUNT = 32;
const MACHINE_CONFIG_PAYLOAD_WIRE_SIZE = 288;
const TEST_CONFIGURATION_PAYLOAD_WIRE_SIZE = 8;
const PLATE_ORIGIN_PAYLOAD_WIRE_SIZE = 12;
const FORCE_PATH_CHUNK_MAX_SAMPLES = 32;
const FORCE_PATH_CHUNK_HEADER_WIRE_SIZE = 12;

const NO_CURRENT_POINT_INDEX = 0xff;

const MessageId = Object.freeze({
  CmdNop: 0x01,
  CmdStop: 0x10,
  CmdPause: 0x11,
  CmdResume: 0x12,
  CmdStart: 0x13,
  CmdClearFault: 0x14,
  CmdHome: 0x15,
  CmdMoveTo: 0x20,
  CmdLoadProgram: 0x30,
  CmdSetSpreader: 0x40,
  CmdSetFixture: 0x41,
  CmdSetMachineConfig: 0x50,
  CmdGetMachineConfig: 0x51,
  CmdSetTestConfiguration: 0x52,
  CmdSetPlateOrigin: 0x53,
  CmdStartProbeCalib: 0x54,
  CmdSetProbeCenter: 0x55,
  CmdDefineMcs: 0x56,
  CmdRestoreMcs: 0x57,
  CmdRequestStatus: 0x7f,
  RspAck: 0x80,
  RspNack: 0x81,
  MsgStatusReport: 0xa0,
  MsgProgramSummary: 0xa1,
  MsgForcePathChunk: 0xa2,
  MsgMachineConfig: 0xa3,
});

const MachineState = Object.freeze({
  Idle: 0,
  Active: 1,
  Paused: 2,
  Stopped: 3,
  Fault: 4,
  Homing: 5,
});

const HomingState = Object.freeze({
  None: 0,
  HomingZ: 1,
  HomingX: 2,
  HomingY: 3,
  Homed: 4,
  HomingError: 5,
});

const StatusFlag = Object.freeze({
  IsHomed: 0,
  LimitWarning: 1,
  CommWatchdog: 2,
  ForceFresh: 3,
  PlateOriginValid: 4,
  ProbeCenterValid: 5,
  McsValid: 6,
});

const MotionState = Object.freeze({
  Idle: 0,
  Moving: 1,
  AtTarget: 2,
});

const ProcessState = Object.freeze({
  Idle: 0,
  Running: 1,
  Completed: 2,
  Aborted: 3,
});

const MACHINE_STATE_NAMES = Object.freeze(['Idle', 'Active', 'Paused', 'Stopped', 'Fault', 'Homing']);
const HOMING_STATE_NAMES = Object.freeze(['None', 'HomingZ', 'HomingX', 'HomingY', 'Homed', 'HomingError']);
const MOTION_STATE_NAMES = Object.freeze(['Idle', 'Moving', 'AtTarget']);
const PROCESS_STATE_NAMES = Object.freeze(['Idle', 'Running', 'Completed', 'Aborted']);
const PROCEDURE_PHASE_NAMES = Object.freeze([
  'Idle',
  'Homing',
  'MoveSafePose',
  'ProbeApproach',
  'ProbeCalibrate',
  'WaitPlateCalibration',
  'PointApproach',
  'PreClampXyCorrect',
  'Clamp',
  'InsertInitial',
  'OptimizeInsert',
  'TestOrAge',
  'Unclamp',
  'NextPoint',
  'MoveSafeEnd',
  'Completed',
  'Aborted',
]);

const FAULT_REASON_NAMES = Object.freeze([
  'None',
  'EStop',
  'ForceHardLimit',
  'ForceCollision',
  'ForceNonFinite',
  'ForceInvalidSample',
  'ForceStale',
  'CommunicationTimeout',
  'ProcedureAbort',
]);

const SPREADER_STATE_NAMES = Object.freeze([
  'Retracted',
  'Extending',
  'Extended',
  'Retracting',
  'SafeDeenergized',
]);

const AckResultCode = Object.freeze({
  Ok: 0,
  Rejected: 1,
  InvalidState: 2,
  ProgramEmpty: 3,
  FaultLatched: 4,
});

const NackErrorCode = Object.freeze({
  Unknown: 0,
  InvalidPayload: 1,
  InvalidState: 2,
  Unsupported: 3,
});

const DecodeResult = Object.freeze({
  Ok: 'Ok',
  NeedMoreData: 'NeedMoreData',
  InvalidFrame: 'InvalidFrame',
  UnsupportedVersion: 'UnsupportedVersion',
  UnknownMessageId: 'UnknownMessageId',
  InvalidLength: 'InvalidLength',
  CrcError: 'CrcError',
  InvalidPayload: 'InvalidPayload',
});

const LimitBit = Object.freeze({
  XMin: 0,
  XMax: 1,
  YMin: 2,
  YMax: 3,
  ZMin: 4,
  ZMax: 5,
});

const KNOWN_MESSAGE_IDS = new Set(Object.values(MessageId));

function isKnownMessageId(messageId) {
  return KNOWN_MESSAGE_IDS.has(messageId & 0xff);
}

function machineStateName(ordinal) {
  return MACHINE_STATE_NAMES[ordinal] || 'Unknown';
}

function homingStateName(ordinal) {
  return HOMING_STATE_NAMES[ordinal] || 'Unknown';
}

function motionStateName(ordinal) {
  return MOTION_STATE_NAMES[ordinal] || 'Unknown';
}

function processStateName(ordinal) {
  return PROCESS_STATE_NAMES[ordinal] || 'Unknown';
}

function procedurePhaseName(ordinal) {
  return PROCEDURE_PHASE_NAMES[ordinal] || 'Unknown';
}

function faultReasonName(ordinal) {
  return FAULT_REASON_NAMES[ordinal] || 'Unknown';
}

function spreaderStateName(ordinal) {
  return SPREADER_STATE_NAMES[ordinal] || 'Unknown';
}

module.exports = {
  MAGIC0,
  MAGIC1,
  PROTOCOL_VERSION,
  MAX_PAYLOAD_SIZE,
  MIN_FRAME_SIZE,
  MAX_FRAME_SIZE,
  MOVE_TO_PAYLOAD_WIRE_SIZE,
  BOOL_PAYLOAD_WIRE_SIZE,
  ACK_PAYLOAD_WIRE_SIZE,
  NACK_PAYLOAD_WIRE_SIZE,
  STATUS_REPORT_PAYLOAD_WIRE_SIZE,
  PROGRAM_SUMMARY_PAYLOAD_WIRE_SIZE,
  LOAD_PROGRAM_MAX_POINT_COUNT,
  MACHINE_CONFIG_PAYLOAD_WIRE_SIZE,
  TEST_CONFIGURATION_PAYLOAD_WIRE_SIZE,
  PLATE_ORIGIN_PAYLOAD_WIRE_SIZE,
  FORCE_PATH_CHUNK_MAX_SAMPLES,
  FORCE_PATH_CHUNK_HEADER_WIRE_SIZE,
  NO_CURRENT_POINT_INDEX,
  MessageId,
  MachineState,
  HomingState,
  StatusFlag,
  MotionState,
  ProcessState,
  MACHINE_STATE_NAMES,
  HOMING_STATE_NAMES,
  MOTION_STATE_NAMES,
  PROCESS_STATE_NAMES,
  PROCEDURE_PHASE_NAMES,
  FAULT_REASON_NAMES,
  SPREADER_STATE_NAMES,
  AckResultCode,
  NackErrorCode,
  DecodeResult,
  LimitBit,
  isKnownMessageId,
  machineStateName,
  homingStateName,
  motionStateName,
  processStateName,
  procedurePhaseName,
  faultReasonName,
  spreaderStateName,
};
