#include "ForceSample.hpp"
#include "ProcessForceModel.hpp"
#include "SafeZ.hpp"
#include "SimProtocolBridge.hpp"

#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "SimPlant.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload, std::size_t payload_length)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static bool dispatchEmpty(simbridge::SimProtocolBridge& bridge, tecomm::MessageId id)
{
    std::vector<simbridge::OutboundFrame> out;
    const tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(id), nullptr, 0);
    bridge.handleFrame(frame, tecomm::DecodeResult::Ok, out);
    return !out.empty();
}

static bool nearEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

static bool startOnePoint(simbridge::SimProtocolBridge& bridge, float target_force, unsigned iterations,
                          TestMode mode)
{
    bridge.machine().setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    bridge.plant().setForceStiffnessNPerMm(5.0f);
    if (!homeMachine(bridge.machine()))
    {
        return false;
    }
    if (!calibrateProbeCenter(bridge.machine()))
    {
        return false;
    }

    simplant::PlantLimits limits{};
    limits.x_min = -100.0f;
    limits.y_min = -100.0f;
    limits.z_min = -100.0f;
    limits.x_max = 200.0f;
    limits.y_max = 200.0f;
    limits.z_max = 200.0f;
    bridge.plant().setLimits(limits);

    TestConfiguration cfg = bridge.machine().testConfiguration();
    cfg.target_force_N = target_force;
    cfg.mode = mode;
    cfg.iterations = iterations;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!bridge.machine().trySetTestConfiguration(cfg, err))
    {
        return false;
    }

    tecomm::LoadProgramPayload load{};
    load.point_count = 1;
    load.points[0] = tecomm::Point3f{5.0f, 2.0f, 0.0f};
    uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
    std::size_t bytes_written = 0;
    tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);
    std::vector<simbridge::OutboundFrame> out;
    tecomm::DecodedFrame load_frame =
        makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), payload, bytes_written);
    bridge.handleFrame(load_frame, tecomm::DecodeResult::Ok, out);
    return dispatchEmpty(bridge, tecomm::MessageId::CmdStart);
}

static int runOnePoint(float target_force, unsigned iterations, TestMode mode, bool verbose)
{
    constexpr float kDt = 0.05f;
    constexpr float k = 5.0f;
    int ticksRemaining = 35000;

    simbridge::SimProtocolBridge bridge;
    {
        MachineConfig mcfg = bridge.machine().machineConfig();
        mcfg.motion.phase1_v_extract = 1.0f;
        mcfg.motion.phase1_v_insert = 1.0f;
        mcfg.motion.phase2_v_extract = 2.0f;
        mcfg.motion.phase2_v_insert = 4.0f;
        mcfg.motion.phase3_v_extract = 2.5f;
        mcfg.motion.phase3_v_insert = 2.5f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!bridge.machine().trySetMachineConfig(mcfg, err))
        {
            return 1;
        }
    }
    if (!startOnePoint(bridge, target_force, iterations, mode))
    {
        return 1;
    }
    if (!nearEqual(bridge.machine().testConfiguration().target_force_N, target_force))
    {
        return 2;
    }

    bool sawInsert = false;
    bool sawOut = false;
    bool sawForceFree = false;
    bool sawReserve = false;
    bool sawIn = false;
    bool sawCompression = false;
    bool sawOptimize = false;
    bool sawTest = false;
    bool poseForceConsistent = true;
    bool sawEndHoming = false;
    bool sawPhase2MotionTicks = false;
    bool sawPhase3MotionTicks = false;
    bool phase1ExtractSpeedOk = false;
    bool phase2InsertSpeedOk = false;
    bool phase3InsertSpeedOk = false;
    int phase2InsertTicks = 0;
    int phase3InsertTicks = 0;
    float prev_z_opt = 1.0e9f;
    float prev_z_test = 1.0e9f;

    float z_test = 0.0f;
    float fz_at_test = 0.0f;
    bool have_tension = false;
    uint8_t prev_leg = 255;
    ProcedurePhase prev_phase = ProcedurePhase::Idle;
    int sample = 0;

    while (bridge.executor().processState() == ProcessState::Running && ticksRemaining-- > 0)
    {
        bridge.tick(kDt);

        const ProcedurePhase ph = bridge.executor().procedurePhase();
        const uint8_t leg = static_cast<uint8_t>(bridge.executor().procedure().cycleLeg());
        const float z = bridge.machine().z();
        const float fz = bridge.machine().hostInputs().force.fz;
        const float vz = bridge.machine().zVelocity();
        const bool coupled = bridge.machine().hostOutputs().spreaderExtended;
        const float target = bridge.machine().testConfiguration().target_force_N;
        const float tol = bridge.machine().machineConfig().phase2.force_tolerance_N;

        if (ph == ProcedurePhase::InsertInitial && !sawInsert)
        {
            sawInsert = true;
            z_test = z;
            if (verbose)
            {
                std::fprintf(stderr,
                             "[runtime] InsertInitial entry z=%.3f vz=%.3f fz=%.3f target=%.3f spreader=%d\n",
                             z, vz, fz, target, coupled ? 1 : 0);
            }
        }
        if (ph == ProcedurePhase::InsertInitial && coupled && fz < fz_at_test)
        {
            fz_at_test = fz;
            have_tension = true;
        }

        if (sawInsert && coupled &&
            (ph == ProcedurePhase::InsertInitial || ph == ProcedurePhase::OptimizeInsert ||
             ph == ProcedurePhase::TestOrAge) &&
            std::fabs(fz) > 0.2f)
        {
            const float e0 = simplant::processFzZ(z, z_test, target, k, true, false);
            const float e1 = simplant::processFzZ(z, z_test, target, k, true, true);
            const float expected = (std::fabs(e0 - fz) < std::fabs(e1 - fz)) ? e0 : e1;
            if (std::fabs(expected - fz) > 1.0f)
            {
                poseForceConsistent = false;
                if (verbose)
                {
                    std::fprintf(stderr, "[runtime] pose/force mismatch z=%.3f fz=%.3f expected=%.3f\n", z, fz,
                                 expected);
                }
            }
        }

        if (ph == ProcedurePhase::InsertInitial)
        {
            if (leg == 0 && vz > 1e-3f)
            {
                sawOut = true;
                if (std::fabs(std::fabs(vz) - 1.0f) < 0.15f)
                {
                    phase1ExtractSpeedOk = true;
                }
            }
            if (leg == 1)
            {
                sawForceFree = true;
                sawReserve = true;
            }
            if (leg == 2)
            {
                sawIn = true;
                if (fz >= target - 0.75f)
                {
                    sawCompression = true;
                }
            }
        }
        if (ph == ProcedurePhase::OptimizeInsert)
        {
            sawOptimize = true;
            if (leg == 0 && std::fabs(vz) > 1e-3f)
            {
                ++phase2InsertTicks;
                if (prev_z_opt < 1.0e8f && std::fabs(z - prev_z_opt) > 1e-4f)
                {
                    sawPhase2MotionTicks = true;
                }
                if (std::fabs(std::fabs(vz) - 4.0f) < 0.25f)
                {
                    phase2InsertSpeedOk = true;
                }
            }
            prev_z_opt = z;
        }
        if (ph == ProcedurePhase::TestOrAge)
        {
            sawTest = true;
            if (leg == 0 && std::fabs(vz) > 1e-3f)
            {
                ++phase3InsertTicks;
                if (prev_z_test < 1.0e8f && std::fabs(z - prev_z_test) > 1e-4f)
                {
                    sawPhase3MotionTicks = true;
                }
                if (std::fabs(std::fabs(vz) - 2.5f) < 0.25f)
                {
                    phase3InsertSpeedOk = true;
                }
            }
            prev_z_test = z;
        }
        if (ph == ProcedurePhase::MoveSafeEnd &&
            bridge.machine().machineState() == MachineState::Homing)
        {
            sawEndHoming = true;
        }

        if (verbose && (ph != prev_phase || leg != prev_leg) && sample < 80)
        {
            std::fprintf(stderr,
                         "[runtime] t=%.2f phase=%u leg=%u z=%.3f vz=%.3f fz=%.3f target=%.3f "
                         "free=%.3f ext=%.3f ins=%.3f spr=%d\n",
                         static_cast<float>(20000 - ticksRemaining) * kDt, static_cast<unsigned>(ph),
                         static_cast<unsigned>(leg), z, vz, fz, target,
                         bridge.executor().procedure().forceFreeZ(),
                         bridge.executor().procedure().extractionZ(),
                         bridge.executor().procedure().insertionZ(), coupled ? 1 : 0);
            ++sample;
        }
        prev_phase = ph;
        prev_leg = leg;
    }

    if (bridge.executor().processState() != ProcessState::Completed)
    {
        return 10;
    }
    if (!sawInsert || !sawOut || !sawForceFree || !sawReserve || !sawIn || !sawCompression || !sawOptimize ||
        !sawTest)
    {
        return 11;
    }
    if (!have_tension || std::fabs(fz_at_test + target_force) > 2.0f)
    {
        return 12;
    }
    const float buffer = bridge.machine().machineConfig().force.insertion_buffer;
    if (!(bridge.executor().procedure().extractionZ() >
          bridge.executor().procedure().forceFreeZ() + buffer - 0.15f))
    {
        return 13;
    }
    if (!(bridge.executor().procedure().insertionZ() < bridge.executor().procedure().forceFreeZ()))
    {
        return 14;
    }
    if (bridge.executor().procedure().testIteration() != iterations)
    {
        return 15;
    }
    if (bridge.machine().testConfiguration().mode != mode)
    {
        return 16;
    }
    if (!poseForceConsistent)
    {
        return 17;
    }
    if (!sawEndHoming)
    {
        return 18;
    }
    if (!phase1ExtractSpeedOk || !phase2InsertSpeedOk || !phase3InsertSpeedOk)
    {
        return 20;
    }
    if (!sawPhase2MotionTicks || phase2InsertTicks < 3 || !sawPhase3MotionTicks ||
        phase3InsertTicks < 3)
    {
        return 21;
    }

    TestProgram ended;
    ended.add(TestPoint{5.0f, 2.0f, 0.0f});
    (void)ended;
    if (!bridge.machine().isHomed() || bridge.machine().homingState() != HomingState::Homed)
    {
        return 22;
    }
    const TestPoint home = bridge.machine().calibrationSet().home_offset;
    if (!nearEqual(bridge.machine().x(), home.x, 0.5f) ||
        !nearEqual(bridge.machine().y(), home.y, 0.5f) ||
        !nearEqual(bridge.machine().z(), home.z, 0.5f))
    {
        return 23;
    }
    return 0;
}

int main()
{
    const int diag = runOnePoint(25.0f, 3u, TestMode::Test, true);
    if (diag != 0)
    {
        std::fprintf(stderr, "diagnostic 25N/3 failed rc=%d\n", diag);
        return diag;
    }

    const float targets[] = {1.0f, 25.0f, 50.0f};
    for (float t : targets)
    {
        const int rc = runOnePoint(t, 3u, TestMode::Test, false);
        if (rc != 0)
        {
            std::fprintf(stderr, "target %.1f failed rc=%d\n", t, rc);
            return 20 + rc;
        }
    }

    const unsigned iters[] = {1u, 3u, 5u};
    for (unsigned n : iters)
    {
        const int rc = runOnePoint(25.0f, n, TestMode::Aging, false);
        if (rc != 0)
        {
            std::fprintf(stderr, "iterations %u failed rc=%d\n", n, rc);
            return 40 + static_cast<int>(n);
        }
    }

    // Phase 2 must not consume GUI iterations: 1 vs 5 still complete via OptimizeInsert.
    {
        const int a = runOnePoint(25.0f, 1u, TestMode::Test, false);
        const int b = runOnePoint(25.0f, 5u, TestMode::Test, false);
        if (a != 0 || b != 0)
        {
            return 50;
        }
    }

    std::fprintf(stderr, "procedure_sim_phases_force_e2e_tests: OK\n");
    return 0;
}
