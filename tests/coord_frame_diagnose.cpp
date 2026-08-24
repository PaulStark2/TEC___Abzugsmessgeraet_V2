// Read-only coordinate-frame diagnosis (no production logic changes).
#include "SimProtocolBridge.hpp"

#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "SafeZ.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static tecomm::DecodedFrame makeDecoded(uint8_t id, const uint8_t* payload = nullptr,
                                        std::size_t len = 0)
{
    tecomm::DecodedFrame f{};
    f.protocol_version = tecomm::kProtocolVersion;
    f.message_id = id;
    f.payload = payload;
    f.payload_length = len;
    return f;
}

static void dump(const char* tag, const Machine& m)
{
    std::printf("%s\n  raw=(%.3f,%.3f,%.3f) machine=(%.3f,%.3f,%.3f) offset=(%.3f,%.3f,%.3f)\n"
                "  isHomed=%d homingState=%u CalibrationSet.home_offset=(%.3f,%.3f,%.3f)\n"
                "  plate_valid=%d plate=(%.3f,%.3f,%.3f) probe_valid=%d probe=(%.3f,%.3f,%.3f)\n"
                "  tool_offset=(%.3f,%.3f,%.3f) cfg.home_offset_R=(%.3f,%.3f,%.3f)\n"
                "  probe_offset_reference=(%.3f,%.3f,%.3f) plantDisc=(%.3f,%.3f,%.3f)\n"
                "  limitMax XYZ=%d%d%d\n",
                tag, m.rawX(), m.rawY(), m.rawZ(), m.x(), m.y(), m.z(), m.referenceOffsetX(),
                m.referenceOffsetY(), m.referenceOffsetZ(), m.isHomed() ? 1 : 0,
                static_cast<unsigned>(m.homingState()), m.calibrationSet().home_offset.x,
                m.calibrationSet().home_offset.y, m.calibrationSet().home_offset.z,
                m.calibrationSet().plate_origin_valid ? 1 : 0, m.calibrationSet().plate_origin.x,
                m.calibrationSet().plate_origin.y, m.calibrationSet().plate_origin.z,
                m.calibrationSet().probe_center_valid ? 1 : 0, m.calibrationSet().probe_center.x,
                m.calibrationSet().probe_center.y, m.calibrationSet().probe_center.z,
                m.calibrationSet().tool_offset.x, m.calibrationSet().tool_offset.y,
                m.calibrationSet().tool_offset.z, m.machineConfig().calibration.home_offset.x,
                m.machineConfig().calibration.home_offset.y,
                m.machineConfig().calibration.home_offset.z,
                m.machineConfig().probe.probe_offset_reference.x,
                m.machineConfig().probe.probe_offset_reference.y,
                m.machineConfig().probe.probe_offset_reference.z,
                0.0f, 0.0f, 0.0f, // filled by caller for plant if needed
                m.hostInputs().limitXMax ? 1 : 0, m.hostInputs().limitYMax ? 1 : 0,
                m.hostInputs().limitZMax ? 1 : 0);
}

static bool homeViaPlant(simbridge::SimProtocolBridge& bridge)
{
    std::vector<simbridge::OutboundFrame> out;
    bridge.handleFrame(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdHome)),
                       tecomm::DecodeResult::Ok, out);
    HomingState last = HomingState::None;
    for (int i = 0; i < 25000; ++i)
    {
        bridge.tick(0.05f);
        const HomingState hs = bridge.machine().homingState();
        if (hs != last)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[home tick=%d phase=%u]", i, static_cast<unsigned>(hs));
            dump(buf, bridge.machine());
            last = hs;
        }
        if (bridge.machine().isHomed() && hs == HomingState::Homed)
        {
            return true;
        }
        if (hs == HomingState::HomingError)
        {
            return false;
        }
    }
    return false;
}

int main()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    const auto disc = bridge.plant().probeDisc();

    std::printf("=== Frame diagnosis (default SimPlant limits) ===\n");
    std::printf("Plant limits: x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]\n",
                bridge.plant().limits().x_min, bridge.plant().limits().x_max,
                bridge.plant().limits().y_min, bridge.plant().limits().y_max,
                bridge.plant().limits().z_min, bridge.plant().limits().z_max);
    std::printf("SimPlant ProbeDisc.center (RAW sampling frame)=(%.3f,%.3f,%.3f)\n", disc.center.x,
                disc.center.y, disc.center.z);
    std::printf("MachineConfig.probe.probe_offset_reference (MACHINE, tool_offset math only)="
                "(%.3f,%.3f,%.3f)\n",
                m.machineConfig().probe.probe_offset_reference.x,
                m.machineConfig().probe.probe_offset_reference.y,
                m.machineConfig().probe.probe_offset_reference.z);

    dump("[before home]", m);

    if (!homeViaPlant(bridge))
    {
        std::printf("FAIL home\n");
        return 1;
    }
    dump("[after full home]", m);

    // Expected physical disc in MACHINE after this home:
    const TestPoint disc_machine{disc.center.x - m.referenceOffsetX(),
                                 disc.center.y - m.referenceOffsetY(),
                                 disc.center.z - m.referenceOffsetZ()};
    std::printf("Implied disc center in MACHINE frame = rawDisc - offset = (%.3f,%.3f,%.3f)\n",
                disc_machine.x, disc_machine.y, disc_machine.z);

    // --- Path 1: GUI-like push of (40,40,10) without M19 ---
    m.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    m.setProbeCenter(TestPoint{40.0f, 40.0f, 10.0f});
    TestProgram prog;
    prog.add(TestPoint{5.0f, 2.0f, 0.0f});
    float safe_z = 0.0f;
    resolveProgramSafeZMachine(prog, m.machineConfig().motion.safe_z_buffer, m.calibrationSet(),
                               safe_z);
    TestPoint pt_machine{};
    toMachinePoint(m.calibrationSet(), TestPoint{5.0f, 2.0f, 0.0f}, pt_machine);
    std::printf("\n=== Path 1: persisted/manual probe=(40,40,10) plate=(0,0,0) point=(5,2,0) ===\n");
    std::printf("  probe_center frame=MACHINE (as stored) value=(40,40,10)\n");
    std::printf("  plate_origin frame=MACHINE value=(0,0,0)\n");
    std::printf("  program point frame=PLATE value=(5,2,0)\n");
    std::printf("  point as MACHINE via toMachinePoint=(%.3f,%.3f,%.3f)\n", pt_machine.x, pt_machine.y,
                pt_machine.z);
    std::printf("  safe_z_plate=maxZ+buffer=30; safe_z_machine=%.3f\n", safe_z);
    std::printf("  First Raise would moveTo(%.3f,%.3f,%.3f) from (%.3f,%.3f,%.3f)\n", m.x(), m.y(),
                safe_z, m.x(), m.y(), m.z());
    std::printf("  Headroom to +Zmax in MACHINE ≈ raw_zmax-offset_z - machine_z = %.3f - %.3f = %.3f\n",
                bridge.plant().limits().z_max - m.referenceOffsetZ(), m.z(),
                (bridge.plant().limits().z_max - m.referenceOffsetZ()) - m.z());

    TestExecutor& ex = bridge.executor();
    ex.setProgram(prog);
    const bool started_bad = ex.start();
    std::printf("  ProcedureController::start()=%d process=%u\n", started_bad ? 1 : 0,
                static_cast<unsigned>(ex.processState()));

    // --- Path 2: Fresh M19 after home near implied disc ---
    std::printf("\n=== Path 2: jog to implied disc MACHINE, run M19 ===\n");
    // Fresh bridge state: stop previous if any
    if (ex.processState() == ProcessState::Running)
    {
        ex.stop();
    }
    // Need isHomed for procedure; M19 only needs HomingState::Homed.
    // Re-home after stop invalidates isHomed.
    if (!homeViaPlant(bridge))
    {
        std::printf("FAIL re-home\n");
        return 2;
    }
    dump("[re-home before M19]", m);

    // Jog near disc in MACHINE (clears isHomed — expected for M19)
    m.moveTo(disc_machine.x, disc_machine.y, disc_machine.z + 5.0f);
    for (int i = 0; i < 5000 && m.motionState() == MotionState::Moving; ++i)
    {
        bridge.tick(0.05f);
    }
    std::printf("  search start pose machine=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f) isHomed=%d\n", m.x(),
                m.y(), m.z(), m.rawX(), m.rawY(), m.rawZ(), m.isHomed() ? 1 : 0);

    if (!ex.startProbeCalibration())
    {
        std::printf("FAIL startProbeCalibration (homingState=%u)\n",
                    static_cast<unsigned>(m.homingState()));
        return 3;
    }
    for (int i = 0; i < 50000 && ex.processState() == ProcessState::Running; ++i)
    {
        bridge.tick(0.05f);
    }
    dump("[after M19]", m);
    std::printf("  M19 result probe_center=(%.3f,%.3f,%.3f) expected≈(%.3f,%.3f,%.3f)\n",
                m.calibrationSet().probe_center.x, m.calibrationSet().probe_center.y,
                m.calibrationSet().probe_center.z, disc_machine.x, disc_machine.y, disc_machine.z);
    std::printf("  tool_offset = probe_offset_reference - probe_center = (%.3f,%.3f,%.3f)\n",
                m.calibrationSet().tool_offset.x, m.calibrationSet().tool_offset.y,
                m.calibrationSet().tool_offset.z);

    // Re-home then start with FRESH probe + plate taught at workspace below home
    if (!homeViaPlant(bridge))
    {
        std::printf("FAIL home after M19\n");
        return 4;
    }
    // Keep M19 probe_center (not overwritten by home — verify)
    dump("[home after M19; probe should be unchanged]", m);

    m.setPlateOrigin(TestPoint{disc_machine.x - 10.0f, disc_machine.y - 10.0f, disc_machine.z - 5.0f});
    prog = TestProgram{};
    prog.add(TestPoint{5.0f, 2.0f, 0.0f});
    resolveProgramSafeZMachine(prog, m.machineConfig().motion.safe_z_buffer, m.calibrationSet(),
                               safe_z);
    toMachinePoint(m.calibrationSet(), TestPoint{5.0f, 2.0f, 0.0f}, pt_machine);
    std::printf("\n=== Path 2b: start with FRESH probe + plate near disc ===\n");
    std::printf("  plate=(%.3f,%.3f,%.3f) probe=(%.3f,%.3f,%.3f) safe_z_machine=%.3f "
                "point_machine=(%.3f,%.3f,%.3f)\n",
                m.calibrationSet().plate_origin.x, m.calibrationSet().plate_origin.y,
                m.calibrationSet().plate_origin.z, m.calibrationSet().probe_center.x,
                m.calibrationSet().probe_center.y, m.calibrationSet().probe_center.z, safe_z,
                pt_machine.x, pt_machine.y, pt_machine.z);
    ex.setProgram(prog);
    const bool started_good = ex.start();
    std::printf("  start()=%d process=%u phase=%u first pose machine=(%.3f,%.3f,%.3f)\n",
                started_good ? 1 : 0, static_cast<unsigned>(ex.processState()),
                static_cast<unsigned>(ex.procedurePhase()), m.x(), m.y(), m.z());
    for (int i = 0; i < 40; ++i)
    {
        bridge.tick(0.05f);
    }
    std::printf("  after 40 ticks process=%u phase=%u machine=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f)\n",
                static_cast<unsigned>(ex.processState()),
                static_cast<unsigned>(ex.procedurePhase()), m.x(), m.y(), m.z(), m.rawX(), m.rawY(),
                m.rawZ());

    return 0;
}
