#include "CommandDispatcher.hpp"

#include "ConfigMapping.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "StatusSnapshot.hpp"

#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

namespace simbridge
{

CommandDispatcher::CommandDispatcher(Machine& machine, TestExecutor& executor)
    : machine_(machine)
    , executor_(executor)
{
}

void CommandDispatcher::dispatch(const tecomm::DecodedFrame& frame, tecomm::DecodeResult decode_result,
                                 std::vector<OutboundFrame>& out)
{
    if (decode_result == tecomm::DecodeResult::UnknownMessageId)
    {
        appendNack(frame.message_id, tecomm::NackErrorCode::Unsupported, 0, out);
        return;
    }

    if (decode_result == tecomm::DecodeResult::InvalidPayload)
    {
        appendNack(frame.message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
        return;
    }

    if (decode_result != tecomm::DecodeResult::Ok)
    {
        return;
    }

    const uint8_t message_id = frame.message_id;

    if (isFaultGuarded(message_id) && machine_.machineState() == MachineState::Fault)
    {
        appendAck(message_id, tecomm::AckResultCode::FaultLatched, out);
        return;
    }

    switch (static_cast<tecomm::MessageId>(message_id))
    {
    case tecomm::MessageId::CmdNop:
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;

    case tecomm::MessageId::CmdStop:
    {
        const MachineState before = machine_.machineState();
        if (executor_.processState() == ProcessState::Running)
        {
            executor_.stop();
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            machine_.stop();
            if (before == MachineState::Fault || before == MachineState::Stopped)
            {
                appendAck(message_id, tecomm::AckResultCode::Rejected, out);
            }
            else
            {
                appendAck(message_id, tecomm::AckResultCode::Ok, out);
            }
        }
        break;
    }

    case tecomm::MessageId::CmdPause:
    {
        if (executor_.processState() == ProcessState::Running && !executor_.pauseAllowed())
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        const MachineState before = machine_.machineState();
        if (executor_.processState() == ProcessState::Running)
        {
            executor_.pause();
        }
        else
        {
            machine_.pause();
        }
        if (machine_.machineState() == MachineState::Paused)
        {
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            appendAck(message_id, tecomm::AckResultCode::Rejected, out);
        }
        (void)before;
        break;
    }

    case tecomm::MessageId::CmdResume:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            executor_.resume();
        }
        else
        {
            machine_.resume();
        }
        if (machine_.machineState() == MachineState::Active)
        {
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            appendAck(message_id, tecomm::AckResultCode::Rejected, out);
        }
        break;
    }

    case tecomm::MessageId::CmdStart:
    {
        if (program_point_count_ == 0)
        {
            appendAck(message_id, tecomm::AckResultCode::ProgramEmpty, out);
            break;
        }
        if (!machine_.calibrationSet().plate_origin_valid)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (!machine_.isHomed())
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (executor_.start())
        {
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            appendAck(message_id, tecomm::AckResultCode::ProgramEmpty, out);
        }
        break;
    }

    case tecomm::MessageId::CmdClearFault:
    {
        const MachineState before = machine_.machineState();
        machine_.clearFault();
        if (before == MachineState::Fault && machine_.machineState() != MachineState::Fault)
        {
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            appendAck(message_id, tecomm::AckResultCode::Rejected, out);
        }
        break;
    }

    case tecomm::MessageId::CmdHome:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (machine_.home())
        {
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            appendAck(message_id, tecomm::AckResultCode::Rejected, out);
        }
        break;
    }

    case tecomm::MessageId::CmdMoveTo:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (machine_.machineState() == MachineState::Homing)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }

        tecomm::MoveToPayload move{};
        if (!tecomm::decodeMoveToPayload(frame.payload, frame.payload_length, move))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }

        machine_.moveTo(move.x, move.y, move.z);
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;
    }

    case tecomm::MessageId::CmdLoadProgram:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }

        tecomm::LoadProgramPayload load{};
        if (!tecomm::decodeLoadProgramPayload(frame.payload, frame.payload_length, load))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }

        TestProgram program;
        for (uint8_t i = 0; i < load.point_count; ++i)
        {
            const TestPoint point{load.points[i].x, load.points[i].y, load.points[i].z};
            if (!program.add(point))
            {
                appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
                return;
            }
        }

        executor_.setProgram(program);
        program_point_count_ = load.point_count;
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        appendProgramSummary(load, out);
        break;
    }

    case tecomm::MessageId::CmdSetSpreader:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        tecomm::BoolPayload value{};
        if (!tecomm::decodeBoolPayload(frame.payload, frame.payload_length, value))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }
        machine_.setSpreaderExtend(value.value != 0);
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;
    }

    case tecomm::MessageId::CmdSetFixture:
    {
        tecomm::BoolPayload value{};
        if (!tecomm::decodeBoolPayload(frame.payload, frame.payload_length, value))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }
        machine_.setFixtureExtend(value.value != 0);
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;
    }

    case tecomm::MessageId::CmdSetMachineConfig:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        tecomm::MachineConfigPayload payload{};
        if (!tecomm::decodeMachineConfigPayload(frame.payload, frame.payload_length, payload))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }
        ConfigValidationError error = ConfigValidationError::Ok;
        if (!machine_.trySetMachineConfig(toMachineConfig(payload), error))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, static_cast<uint8_t>(error), out);
            break;
        }
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;
    }

    case tecomm::MessageId::CmdGetMachineConfig:
    {
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        {
            OutboundFrame frame_out{};
            const tecomm::EncodeResult encoded = tecomm::encodeMessage(
                static_cast<uint8_t>(tecomm::MessageId::MsgMachineConfig),
                toMachineConfigPayload(machine_.machineConfig()), frame_out.data, sizeof(frame_out.data));
            if (encoded.ok)
            {
                frame_out.length = encoded.bytes_written;
                out.push_back(frame_out);
            }
        }
        break;
    }

    case tecomm::MessageId::CmdSetTestConfiguration:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        tecomm::TestConfigurationPayload payload{};
        if (!tecomm::decodeTestConfigurationPayload(frame.payload, frame.payload_length, payload))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }
        ConfigValidationError error = ConfigValidationError::Ok;
        if (!machine_.trySetTestConfiguration(toTestConfiguration(payload), error))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, static_cast<uint8_t>(error), out);
            break;
        }
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;
    }

    case tecomm::MessageId::CmdSetPlateOrigin:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        tecomm::MoveToPayload pose{};
        if (!tecomm::decodeMoveToPayload(frame.payload, frame.payload_length, pose))
        {
            appendNack(message_id, tecomm::NackErrorCode::InvalidPayload, 0, out);
            break;
        }
        machine_.setPlateOrigin(TestPoint{pose.x, pose.y, pose.z});
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        break;
    }

    case tecomm::MessageId::CmdStartProbeCalib:
    {
        if (executor_.processState() == ProcessState::Running)
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (!machine_.isHomed())
        {
            appendAck(message_id, tecomm::AckResultCode::InvalidState, out);
            break;
        }
        if (executor_.startProbeCalibration())
        {
            appendAck(message_id, tecomm::AckResultCode::Ok, out);
        }
        else
        {
            appendAck(message_id, tecomm::AckResultCode::Rejected, out);
        }
        break;
    }

    case tecomm::MessageId::CmdRequestStatus:
        appendAck(message_id, tecomm::AckResultCode::Ok, out);
        appendStatusReport(out);
        break;

    default:
        appendNack(message_id, tecomm::NackErrorCode::Unsupported, 0, out);
        break;
    }
}

uint8_t CommandDispatcher::programPointCount() const
{
    return program_point_count_;
}

void CommandDispatcher::setProgramPointCount(uint8_t count)
{
    program_point_count_ = count;
}

void CommandDispatcher::appendAck(uint8_t ref_message_id, tecomm::AckResultCode result,
                                  std::vector<OutboundFrame>& out)
{
    tecomm::AckPayload payload{};
    payload.ref_message_id = ref_message_id;
    payload.result_code = static_cast<uint8_t>(result);

    OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::RspAck), payload, frame.data, sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }
}

void CommandDispatcher::appendNack(uint8_t ref_message_id, tecomm::NackErrorCode error, uint8_t detail,
                                   std::vector<OutboundFrame>& out)
{
    tecomm::NackPayload payload{};
    payload.ref_message_id = ref_message_id;
    payload.error_code = static_cast<uint8_t>(error);
    payload.detail = detail;

    OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::RspNack), payload, frame.data, sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }
}

void CommandDispatcher::appendStatusReport(std::vector<OutboundFrame>& out)
{
    const tecomm::StatusReportPayload status =
        buildStatusReport(machine_, executor_, program_point_count_);

    OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport), status, frame.data, sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }
}

void CommandDispatcher::appendProgramSummary(const tecomm::LoadProgramPayload& loaded,
                                             std::vector<OutboundFrame>& out)
{
    tecomm::ProgramSummaryPayload summary{};
    summary.point_count = loaded.point_count;
    summary.current_index = 0;

    if (loaded.point_count == 0)
    {
        summary.x_min = 0.0f;
        summary.x_max = 0.0f;
        summary.y_min = 0.0f;
        summary.y_max = 0.0f;
        summary.z_min = 0.0f;
        summary.z_max = 0.0f;
    }
    else
    {
        summary.x_min = loaded.points[0].x;
        summary.x_max = loaded.points[0].x;
        summary.y_min = loaded.points[0].y;
        summary.y_max = loaded.points[0].y;
        summary.z_min = loaded.points[0].z;
        summary.z_max = loaded.points[0].z;

        for (uint8_t i = 1; i < loaded.point_count; ++i)
        {
            const tecomm::Point3f& p = loaded.points[i];
            if (p.x < summary.x_min)
            {
                summary.x_min = p.x;
            }
            if (p.x > summary.x_max)
            {
                summary.x_max = p.x;
            }
            if (p.y < summary.y_min)
            {
                summary.y_min = p.y;
            }
            if (p.y > summary.y_max)
            {
                summary.y_max = p.y;
            }
            if (p.z < summary.z_min)
            {
                summary.z_min = p.z;
            }
            if (p.z > summary.z_max)
            {
                summary.z_max = p.z;
            }
        }
    }

    OutboundFrame frame{};
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::MsgProgramSummary), summary, frame.data, sizeof(frame.data));
    if (encoded.ok)
    {
        frame.length = encoded.bytes_written;
        out.push_back(frame);
    }
}

bool CommandDispatcher::isFaultGuarded(uint8_t message_id) const
{
    switch (static_cast<tecomm::MessageId>(message_id))
    {
    case tecomm::MessageId::CmdNop:
    case tecomm::MessageId::CmdClearFault:
    case tecomm::MessageId::CmdRequestStatus:
    case tecomm::MessageId::CmdGetMachineConfig:
        return false;
    default:
        return true;
    }
}

} // namespace simbridge
