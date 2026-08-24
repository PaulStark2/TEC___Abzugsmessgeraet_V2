// Verification-only: MCS define vs normal re-home (no production changes).
#include "SimProtocolBridge.hpp"

#include "MessageIds.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

tecomm::DecodedFrame emptyHome()
{
    tecomm::DecodedFrame f{};
    f.protocol_version = tecomm::kProtocolVersion;
    f.message_id = static_cast<uint8_t>(tecomm::MessageId::CmdHome);
    return f;
}

bool nearEqual(float a, float b, float eps = 0.15f)
{
    return std::fabs(a - b) <= eps;
}

bool nearPoint(const TestPoint& a, const TestPoint& b, float eps = 0.15f)
{
    return nearEqual(a.x, b.x, eps) && nearEqual(a.y, b.y, eps) && nearEqual(a.z, b.z, eps);
}

void printPose(const char* tag, const Machine& m)
{
    std::printf("%s\n"
                "  raw=(%.4f, %.4f, %.4f)\n"
                "  machine=(%.4f, %.4f, %.4f)\n"
                "  offset=(%.4f, %.4f, %.4f)\n"
                "  home_offset=(%.4f, %.4f, %.4f)\n"
                "  mcs_valid=%d isHomed=%d homingState=%u\n"
                "  plate_valid=%d plate=(%.4f, %.4f, %.4f)\n"
                "  probe_valid=%d probe=(%.4f, %.4f, %.4f)\n"
                "  tool=(%.4f, %.4f, %.4f)\n",
                tag, m.rawX(), m.rawY(), m.rawZ(), m.x(), m.y(), m.z(), m.referenceOffsetX(),
                m.referenceOffsetY(), m.referenceOffsetZ(), m.calibrationSet().home_offset.x,
                m.calibrationSet().home_offset.y, m.calibrationSet().home_offset.z,
                m.machineCoordinateSystemValid() ? 1 : 0, m.isHomed() ? 1 : 0,
                static_cast<unsigned>(m.homingState()),
                m.calibrationSet().plate_origin_valid ? 1 : 0, m.calibrationSet().plate_origin.x,
                m.calibrationSet().plate_origin.y, m.calibrationSet().plate_origin.z,
                m.calibrationSet().probe_center_valid ? 1 : 0, m.calibrationSet().probe_center.x,
                m.calibrationSet().probe_center.y, m.calibrationSet().probe_center.z,
                m.calibrationSet().tool_offset.x, m.calibrationSet().tool_offset.y,
                m.calibrationSet().tool_offset.z);
}

bool runHome(simbridge::SimProtocolBridge& bridge, const char* label, int max_ticks = 25000)
{
    std::printf("\n=== HOMING START: %s | mcs_valid_before=%d ===\n", label,
                bridge.machine().machineCoordinateSystemValid() ? 1 : 0);
    const bool expect_define = !bridge.machine().machineCoordinateSystemValid();
    std::printf("Expected path: %s\n", expect_define ? "DEFINE MCS (first home / !mcs_valid)"
                                                       : "REFERENCE EXISTING MCS (normal home)");

    std::vector<simbridge::OutboundFrame> out;
    bridge.handleFrame(emptyHome(), tecomm::DecodeResult::Ok, out);

    HomingState last = HomingState::None;
    bool retracting_note = false;
    for (int i = 0; i < max_ticks; ++i)
    {
        const HomingState hs = bridge.machine().homingState();
        if (hs != last)
        {
            std::printf("[tick=%d] phase=%u machine=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f) "
                        "offset=(%.3f,%.3f,%.3f) limMax=%d%d%d\n",
                        i, static_cast<unsigned>(hs), bridge.machine().x(), bridge.machine().y(),
                        bridge.machine().z(), bridge.machine().rawX(), bridge.machine().rawY(),
                        bridge.machine().rawZ(), bridge.machine().referenceOffsetX(),
                        bridge.machine().referenceOffsetY(), bridge.machine().referenceOffsetZ(),
                        bridge.machine().hostInputs().limitXMax ? 1 : 0,
                        bridge.machine().hostInputs().limitYMax ? 1 : 0,
                        bridge.machine().hostInputs().limitZMax ? 1 : 0);
            last = hs;
            retracting_note = false;
        }
        // Detect retract: limit just cleared while still in same homing axis phase with motion.
        if ((hs == HomingState::HomingZ || hs == HomingState::HomingX || hs == HomingState::HomingY) &&
            bridge.machine().motionState() == MotionState::Moving && !retracting_note)
        {
            const bool lim =
                (hs == HomingState::HomingZ && bridge.machine().hostInputs().limitZMax) ||
                (hs == HomingState::HomingX && bridge.machine().hostInputs().limitXMax) ||
                (hs == HomingState::HomingY && bridge.machine().hostInputs().limitYMax);
            if (!lim && i > 0)
            {
                // soft note once per phase when moving off switch region
            }
        }
        bridge.tick(0.05f);
        if (bridge.machine().isHomed() && bridge.machine().homingState() == HomingState::Homed)
        {
            std::printf("=== HOMING DONE: %s ===\n", label);
            printPose("after home", bridge.machine());
            return true;
        }
        if (bridge.machine().homingState() == HomingState::HomingError)
        {
            std::printf("HomingError during %s\n", label);
            return false;
        }
    }
    std::printf("TIMEOUT during %s\n", label);
    return false;
}

bool moveToSettle(simbridge::SimProtocolBridge& bridge, float x, float y, float z)
{
    Machine& m = bridge.machine();
    m.moveTo(x, y, z);
    for (int i = 0; i < 8000 && m.motionState() == MotionState::Moving; ++i)
    {
        bridge.tick(0.05f);
    }
    return nearEqual(m.x(), x, 0.25f) && nearEqual(m.y(), y, 0.25f) && nearEqual(m.z(), z, 0.25f);
}

struct Snapshot
{
    TestPoint machine{};
    TestPoint raw{};
    TestPoint offset{};
    TestPoint home{};
    TestPoint plate{};
    TestPoint probe{};
    TestPoint tool{};
    bool mcs = false;
    bool plate_v = false;
    bool probe_v = false;
};

Snapshot snap(const Machine& m)
{
    Snapshot s{};
    s.machine = TestPoint{m.x(), m.y(), m.z()};
    s.raw = TestPoint{m.rawX(), m.rawY(), m.rawZ()};
    s.offset = TestPoint{m.referenceOffsetX(), m.referenceOffsetY(), m.referenceOffsetZ()};
    s.home = m.calibrationSet().home_offset;
    s.plate = m.calibrationSet().plate_origin;
    s.probe = m.calibrationSet().probe_center;
    s.tool = m.calibrationSet().tool_offset;
    s.mcs = m.machineCoordinateSystemValid();
    s.plate_v = m.calibrationSet().plate_origin_valid;
    s.probe_v = m.calibrationSet().probe_center_valid;
    return s;
}

void printCriteria(const char* name, bool ok)
{
    std::printf("CRITERION %-40s %s\n", name, ok ? "PASS" : "FAIL");
}

} // namespace

int main()
{
    std::printf("============================================================\n");
    std::printf("VERIFICATION: MCS define vs normal re-home (read-only)\n");
    std::printf("============================================================\n");
    std::printf("Code paths (no production edits):\n");
    std::printf("  DEFINE:  Machine::home() when !mcs_valid -> HomingMode::DefineMcs\n");
    std::printf("           beginHomingRetract: machine := raw (identity)\n");
    std::printf("           finishHomingSuccess: latch home_offset, mcs_valid=true,\n");
    std::printf("                                clear plate/probe validity\n");
    std::printf("  REFERENCE: Machine::home() when mcs_valid -> HomingMode::Reference\n");
    std::printf("           beginHomingRetract: machine := home_offset + R\n");
    std::printf("           finishHomingSuccess: leave home/plate/probe/tool unchanged\n");
    std::printf("============================================================\n");

    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    int fails = 0;

    // --- Step 1: Initial / DEFINE MCS ---
    if (!runHome(bridge, "STEP1_INITIAL_DEFINE"))
    {
        return 1;
    }
    if (!m.machineCoordinateSystemValid())
    {
        std::printf("FAIL: MCS not valid after initial home\n");
        return 2;
    }
    const Snapshot after_define = snap(m);

    // --- Step 2: Calibration (GUI-persist style, machine frame) ---
    m.setPlateOrigin(TestPoint{55.0f, 57.0f, 17.0f});
    m.setProbeCenter(TestPoint{40.0f, 40.0f, 10.0f});
    m.setToolOffset(TestPoint{-0.000072f, -0.000061f, 0.000049f});
    const Snapshot calib = snap(m);
    std::printf("\n=== STEP2_CALIBRATION ===\n");
    printPose("after set plate/probe/tool", m);
    const TestPoint probe_before = calib.probe;
    const TestPoint plate_before = calib.plate;
    const TestPoint tool_before = calib.tool;
    const TestPoint home_before = calib.home;
    const TestPoint offset_before = calib.offset;

    // --- Step 3: Leave home ---
    const float hx = home_before.x;
    const float hy = home_before.y;
    const float hz = home_before.z;
    std::printf("\n=== STEP3_LEAVE_HOME ===\n");
    if (!moveToSettle(bridge, hx - 20.0f, hy - 15.0f, hz - 10.0f))
    {
        std::printf("FAIL move P1\n");
        return 3;
    }
    std::printf("P1 machine=(%.3f,%.3f,%.3f) isHomed=%d\n", m.x(), m.y(), m.z(), m.isHomed() ? 1 : 0);
    if (!moveToSettle(bridge, hx - 35.0f, hy - 25.0f, hz - 20.0f))
    {
        std::printf("FAIL move P2\n");
        return 4;
    }
    std::printf("P2 machine=(%.3f,%.3f,%.3f) isHomed=%d\n", m.x(), m.y(), m.z(), m.isHomed() ? 1 : 0);
    if (!moveToSettle(bridge, hx - 12.0f, hy - 8.0f, hz - 6.0f))
    {
        std::printf("FAIL move P3\n");
        return 5;
    }
    std::printf("P3 machine=(%.3f,%.3f,%.3f) isHomed=%d\n", m.x(), m.y(), m.z(), m.isHomed() ? 1 : 0);
    if (m.isHomed())
    {
        std::printf("FAIL: still isHomed after leaving home\n");
        return 6;
    }
    const Snapshot before_loss = snap(m);
    printPose("before step loss", m);

    // --- Step 4: Artificial step loss ---
    constexpr float kDx = -5.0f;
    constexpr float kDy = 3.0f;
    constexpr float kDz = -2.0f;
    std::printf("\n=== STEP4_STEP_LOSS inject Δ=(%.1f,%.1f,%.1f) via shiftRawPose ===\n", kDx, kDy,
                kDz);
    m.shiftRawPose(kDx, kDy, kDz);
    m.invalidateHoming();
    const Snapshot after_loss = snap(m);
    printPose("after step loss", m);
    const bool loss_visible =
        !nearEqual(after_loss.raw.x, before_loss.raw.x, 0.5f) ||
        !nearEqual(after_loss.machine.x, before_loss.machine.x, 0.5f);
    printCriteria("step loss visible in pose", loss_visible);
    if (!loss_visible)
    {
        ++fails;
    }

    // --- Step 5/6: NORMAL re-home ---
    if (!runHome(bridge, "STEP5_NORMAL_REFERENCE"))
    {
        return 7;
    }
    if (!m.machineCoordinateSystemValid())
    {
        std::printf("FAIL: MCS cleared by normal home\n");
        return 8;
    }
    const Snapshot after_rehome = snap(m);

    // Acceptance
    std::printf("\n=== ACCEPTANCE after first normal re-home ===\n");
    const bool c1 = nearPoint(after_rehome.machine, home_before, 0.35f);
    printCriteria("1 machine at calibrated home", c1);
    if (!c1)
    {
        ++fails;
    }

    // Offsets: after identity define they are ~0; after Reference they are recomputed
    // from raw_at_switch - (home+R). With step loss, offset changes to correct MCS.
    // Semantically MCS is the SAME if home_offset unchanged and machine returns to home.
    const bool c2_home = nearPoint(after_rehome.home, home_before, 1e-4f);
    const bool c2_mcs = after_rehome.mcs;
    printCriteria("2a home_offset unchanged (MCS latch)", c2_home);
    printCriteria("2b mcs_valid still true", c2_mcs);
    if (!c2_home || !c2_mcs)
    {
        ++fails;
    }
    // Note on offsets: they MAY change after step-loss correction (that's the fix).
    // Document delta:
    std::printf("  offset_before=(%.4f,%.4f,%.4f)\n  offset_after =(%.4f,%.4f,%.4f)\n"
                "  (offset may change to absorb step loss; home_offset must not)\n",
                offset_before.x, offset_before.y, offset_before.z, after_rehome.offset.x,
                after_rehome.offset.y, after_rehome.offset.z);

    const bool c3 = nearPoint(after_rehome.probe, probe_before, 1e-4f) &&
                    nearPoint(after_rehome.plate, plate_before, 1e-4f) &&
                    nearPoint(after_rehome.tool, tool_before, 1e-4f) && after_rehome.plate_v &&
                    after_rehome.probe_v;
    printCriteria("3 probe/plate/tool unchanged", c3);
    if (!c3)
    {
        ++fails;
    }

    // Position error: machine should match home; raw should be home + offset
    // (consistent). Error vs pre-loss home is already c1.
    const bool c4 = c1;
    printCriteria("4 step loss corrected (at home again)", c4);
    if (!c4)
    {
        ++fails;
    }

    const bool c5 = c2_home;
    printCriteria("5 calibrated home point unchanged", c5);
    if (!c5)
    {
        ++fails;
    }

    // --- Three successive normal re-homes ---
    std::printf("\n=== THREE SUCCESSIVE NORMAL RE-HOMES ===\n");
    Snapshot prev = after_rehome;
    for (int n = 1; n <= 3; ++n)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "REHOME_%d", n);
        if (!moveToSettle(bridge, hx - 8.0f * static_cast<float>(n), hy - 5.0f, hz - 4.0f))
        {
            std::printf("FAIL leave home before %s\n", label);
            return 10 + n;
        }
        if (!runHome(bridge, label))
        {
            return 20 + n;
        }
        const Snapshot cur = snap(m);
        const bool ok = nearPoint(cur.machine, home_before, 0.35f) &&
                        nearPoint(cur.home, home_before, 1e-4f) &&
                        nearPoint(cur.probe, probe_before, 1e-4f) &&
                        nearPoint(cur.plate, plate_before, 1e-4f) &&
                        nearPoint(cur.tool, tool_before, 1e-4f) && cur.mcs;
        printCriteria(label, ok);
        if (!ok)
        {
            ++fails;
            printPose("failed snapshot", m);
        }
        prev = cur;
    }
    (void)prev;
    (void)after_define;
    (void)after_loss;

    // --- Optional robustness: several deltas ---
    std::printf("\n=== OPTIONAL ROBUSTNESS (multiple deltas) ===\n");
    const float deltas[][3] = {{-1, 0, 0}, {-5, 0, 0}, {5, 0, 0}, {0, -5, 0}, {0, 0, -5}, {-2, 4, -3}};
    for (const auto& d : deltas)
    {
        if (!moveToSettle(bridge, hx - 10.0f, hy - 10.0f, hz - 5.0f))
        {
            std::printf("FAIL pre-move for delta\n");
            ++fails;
            continue;
        }
        m.shiftRawPose(d[0], d[1], d[2]);
        m.invalidateHoming();
        char lab[80];
        std::snprintf(lab, sizeof(lab), "robust Δ=(%.0f,%.0f,%.0f)", d[0], d[1], d[2]);
        if (!runHome(bridge, lab))
        {
            printCriteria(lab, false);
            ++fails;
            continue;
        }
        const Snapshot s = snap(m);
        const bool ok = nearPoint(s.machine, home_before, 0.5f) &&
                        nearPoint(s.home, home_before, 1e-4f) &&
                        nearPoint(s.probe, probe_before, 1e-4f) &&
                        nearPoint(s.plate, plate_before, 1e-4f) && s.mcs;
        printCriteria(lab, ok);
        if (!ok)
        {
            ++fails;
        }
    }

    std::printf("\n============================================================\n");
    std::printf("SUMMARY fails=%d → %s\n", fails, fails == 0 ? "ALL ACCEPTANCE PASS" : "FAILURES");
    std::printf("============================================================\n");
    return fails == 0 ? 0 : 100;
}
