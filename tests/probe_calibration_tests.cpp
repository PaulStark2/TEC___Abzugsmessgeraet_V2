#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "ProbeCalibration.hpp"
#include "ProbeGeometry.hpp"

#include <cmath>
#include <vector>

static bool nearEqual(float a, float b, float eps = 0.6f)
{
    return std::fabs(a - b) <= eps;
}

static bool applyFastProbeConfig(Machine& machine)
{
    MachineConfig cfg = machine.machineConfig();
    cfg.probe.num_fine_searches = 0;
    cfg.probe.search_speed = 15.0f;
    cfg.probe.fine_search_speed = 5.0f;
    cfg.motion.v_approach = 25.0f;
    ConfigValidationError err = ConfigValidationError::Ok;
    return machine.trySetMachineConfig(cfg, err);
}

static HostInputs withDiscTrigger(const Machine& machine, const TestPoint& disc, HostInputs in = HostInputs{})
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    if (toolContactsProbeDisc(machine.x(), machine.y(), machine.z(), disc.x, disc.y, disc.z,
                              probe.measuring_head_diameter, probe.measuring_head_thickness,
                              probe.tool_diameter, probe.tool_cylindrical_height))
    {
        in.probeTriggered = true;
        in.probeTriggerPoseValid = true;
        in.probeTriggerX = machine.x();
        in.probeTriggerY = machine.y();
        in.probeTriggerZ = machine.z();
        in.probeTriggerSequence = 1;
    }
    return withFreshForce(in);
}

static HostInputs withManualProbeTrigger(const TestPoint& latched_tcp_pose, bool pose_valid,
                                          std::uint32_t trigger_sequence)
{
    HostInputs in{};
    in.probeTriggered = true;
    in.probeTriggerPoseValid = pose_valid;
    in.probeTriggerX = latched_tcp_pose.x;
    in.probeTriggerY = latched_tcp_pose.y;
    in.probeTriggerZ = latched_tcp_pose.z;
    in.probeTriggerSequence = trigger_sequence;
    return withFreshForce(in);
}

static bool runCalib(ProbeCalibration& calib, Machine& machine, const TestPoint& disc, int max_ticks)
{
    for (int i = 0; i < max_ticks && !calib.isComplete() && !calib.isError(); ++i)
    {
        machine.tick(0.05f);
        machine.setHostInputs(withDiscTrigger(machine, disc));
        calib.tick(0.05f, machine);
    }
    return calib.isComplete() && !calib.isError();
}

static void moveMachineTo(Machine& machine, const TestPoint& target, int max_ticks = 800)
{
    machine.moveTo(target.x, target.y, target.z);
    for (int i = 0; i < max_ticks; ++i)
    {
        machine.setHostInputs(withFreshForce(HostInputs{}));
        machine.tick(0.05f);
        if (machine.motionState() == MotionState::AtTarget)
        {
            return;
        }
    }
}

int main()
{
    // Search end without trigger is not a valid result and does not store a center.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 1;
        }
        ProbeCalibration calib;
        if (!calib.start(m, m.machineConfig().probe.probe_offset_reference))
        {
            return 2;
        }
        for (int i = 0; i < 2000 && !calib.isComplete() && !calib.isError(); ++i)
        {
            m.setHostInputs(withFreshForce(HostInputs{}));
            m.tick(0.05f);
            calib.tick(0.05f, m);
        }
        if (calib.isComplete() || !calib.isError())
        {
            return 3;
        }
        if (m.calibrationSet().probe_center_valid)
        {
            return 4;
        }
    }

    // Full protocol against a disc at the nominal center.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 10;
        }
        const TestPoint disc = m.machineConfig().probe.probe_offset_reference;
        ProbeCalibration calib;
        if (!calib.start(m, disc) || !runCalib(calib, m, disc, 4000))
        {
            return 11;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 12;
        }
        const TestPoint center = m.calibrationSet().probe_center;
        if (!nearEqual(center.x, disc.x) || !nearEqual(center.y, disc.y) || !nearEqual(center.z, disc.z))
        {
            return 13;
        }
        const TestPoint offset = m.calibrationSet().tool_offset;
        if (!nearEqual(offset.x, 0.0f) || !nearEqual(offset.y, 0.0f) || !nearEqual(offset.z, 0.0f))
        {
            return 14;
        }
    }

    // Offset disc: tool_offset = nominal - measured, replace not accumulate.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 20;
        }
        const TestPoint nominal = m.machineConfig().probe.probe_offset_reference;
        const TestPoint disc{nominal.x + 1.0f, nominal.y - 2.0f, nominal.z + 1.0f};
        ProbeCalibration calib;
        if (!calib.start(m, disc) || !runCalib(calib, m, disc, 4000))
        {
            return 21;
        }
        const TestPoint measured = m.calibrationSet().probe_center;
        const TestPoint offset = m.calibrationSet().tool_offset;
        if (!nearEqual(offset.x, nominal.x - measured.x) || !nearEqual(offset.y, nominal.y - measured.y) ||
            !nearEqual(offset.z, nominal.z - measured.z))
        {
            return 22;
        }
        if (!nearEqual(offset.x, -1.0f, 0.8f) || !nearEqual(offset.y, 2.0f, 0.8f) ||
            !nearEqual(offset.z, -1.0f, 0.8f))
        {
            return 23;
        }

        ProbeCalibration calib2;
        if (!calib2.start(m, disc) || !runCalib(calib2, m, disc, 4000))
        {
            return 24;
        }
        const TestPoint offset2 = m.calibrationSet().tool_offset;
        if (!nearEqual(offset2.x, offset.x, 0.8f) || !nearEqual(offset2.y, offset.y, 0.8f) ||
            !nearEqual(offset2.z, offset.z, 0.8f))
        {
            return 25;
        }
        if (std::fabs(offset2.x) > 3.0f)
        {
            return 26;
        }
    }

    // Abort while searching is an error without a stored center.
    {
        Machine m;
        applyFastProbeConfig(m);
        ProbeCalibration calib;
        calib.start(m, m.machineConfig().probe.probe_offset_reference);
        calib.abort();
        if (!calib.isError() || calib.isComplete())
        {
            return 30;
        }
        if (m.calibrationSet().probe_center_valid)
        {
            return 31;
        }
    }

    // Fault during search is an error. Previous valid calibration is kept.
    {
        Machine m;
        applyFastProbeConfig(m);
        const TestPoint disc = m.machineConfig().probe.probe_offset_reference;
        ProbeCalibration first;
        if (!first.start(m, disc) || !runCalib(first, m, disc, 4000))
        {
            return 40;
        }
        const TestPoint kept_center = m.calibrationSet().probe_center;
        const TestPoint kept_offset = m.calibrationSet().tool_offset;

        ProbeCalibration calib;
        calib.start(m, m.machineConfig().probe.probe_offset_reference);
        HostInputs in{};
        in.e_stop = true;
        m.setHostInputs(withFreshForce(in));
        m.tick(0.05f);
        calib.tick(0.05f, m);
        if (!calib.isError())
        {
            return 41;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 42;
        }
        if (!nearEqual(m.calibrationSet().probe_center.x, kept_center.x) ||
            !nearEqual(m.calibrationSet().tool_offset.x, kept_offset.x))
        {
            return 43;
        }
    }

    // Failed run does not overwrite a previous valid offset.
    {
        Machine m;
        applyFastProbeConfig(m);
        const TestPoint disc = m.machineConfig().probe.probe_offset_reference;
        ProbeCalibration first;
        if (!first.start(m, disc) || !runCalib(first, m, disc, 4000))
        {
            return 50;
        }
        const TestPoint kept_center = m.calibrationSet().probe_center;
        const TestPoint kept = m.calibrationSet().tool_offset;
        ProbeCalibration fail;
        fail.start(m, m.machineConfig().probe.probe_offset_reference);
        for (int i = 0; i < 2500 && !fail.isComplete() && !fail.isError(); ++i)
        {
            m.setHostInputs(withFreshForce(HostInputs{}));
            m.tick(0.05f);
            fail.tick(0.05f, m);
        }
        if (!fail.isError())
        {
            return 51;
        }
        if (!nearEqual(m.calibrationSet().tool_offset.x, kept.x) ||
            !nearEqual(m.calibrationSet().tool_offset.y, kept.y) ||
            !nearEqual(m.calibrationSet().tool_offset.z, kept.z))
        {
            return 52;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 53;
        }
        if (!nearEqual(m.calibrationSet().probe_center.x, kept_center.x) ||
            !nearEqual(m.calibrationSet().probe_center.y, kept_center.y) ||
            !nearEqual(m.calibrationSet().probe_center.z, kept_center.z))
        {
            return 54;
        }
    }

    // XY contact Z stays at measured center Z minus search_depth (not disc mid-plane).
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 60;
        }
        const TestPoint disc = m.machineConfig().probe.probe_offset_reference;
        ProbeSearch search;
        ProbeSearch::Request r{};
        r.reference = disc;
        r.dir = ProbeSearchDir::PosX;
        r.approach_distance = 10.0f;
        r.search_distance = m.machineConfig().probe.search_distance;
        r.search_depth = m.machineConfig().probe.search_depth;
        r.lateral_offset = xyStartOffset(r.search_distance, 8.0f, 2.5f);
        r.xy_inner_radius = xySearchInnerRadius(8.0f, 2.0f);
        r.backoff = 1.0f;
        r.approach_speed = 25.0f;
        r.fast_speed = 15.0f;
        r.fine_speed = 5.0f;
        r.num_fine_searches = 0;
        if (!search.start(m, r))
        {
            return 61;
        }
        for (int i = 0; i < 1200 && !search.isComplete() && !search.isError(); ++i)
        {
            m.tick(0.05f);
            m.setHostInputs(withDiscTrigger(m, disc));
            search.tick(0.05f, m);
        }
        if (!search.isComplete())
        {
            return 62;
        }
        const float expected_z = disc.z - m.machineConfig().probe.search_depth;
        if (!nearEqual(search.result().z, expected_z, 0.6f))
        {
            return 63;
        }
    }

    // Measurement order must be: Z → -Y → +Y → -X → +X.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 70;
        }
        const TestPoint disc = m.machineConfig().probe.probe_offset_reference;
        ProbeCalibration calib;
        if (!calib.start(m, disc))
        {
            return 71;
        }

        std::vector<ProbeCalibration::Step> steps;
        steps.push_back(calib.step());

        for (int i = 0; i < 5000 && !calib.isComplete() && !calib.isError(); ++i)
        {
            m.tick(0.05f);
            m.setHostInputs(withDiscTrigger(m, disc));
            calib.tick(0.05f, m);
            if (steps.empty() || steps.back() != calib.step())
            {
                steps.push_back(calib.step());
            }
        }

        if (!calib.isComplete())
        {
            return 72;
        }
        if (steps.size() < 5)
        {
            return 73;
        }
        if (steps[0] != ProbeCalibration::Step::MeasureZ || steps[1] != ProbeCalibration::Step::MeasureNegY ||
            steps[2] != ProbeCalibration::Step::MeasurePosY || steps[3] != ProbeCalibration::Step::MeasureNegX ||
            steps[4] != ProbeCalibration::Step::MeasurePosX)
        {
            return 74;
        }
    }

    // head_diameter is intentionally irrelevant: probe_center/tool_offset must not change.
    {
        Machine m1;
        if (!applyFastProbeConfig(m1))
        {
            return 80;
        }
        const TestPoint disc1 = m1.machineConfig().probe.probe_offset_reference;
        ProbeCalibration c1;
        if (!c1.start(m1, disc1))
        {
            return 81;
        }
        for (int i = 0; i < 4000 && !c1.isComplete() && !c1.isError(); ++i)
        {
            m1.tick(0.05f);
            m1.setHostInputs(withDiscTrigger(m1, disc1));
            c1.tick(0.05f, m1);
        }
        if (!c1.isComplete())
        {
            return 82;
        }
        const TestPoint kept_center = m1.calibrationSet().probe_center;
        const TestPoint kept_offset = m1.calibrationSet().tool_offset;

        // head_diameter = 0
        {
            Machine m0;
            if (!applyFastProbeConfig(m0))
            {
                return 83;
            }
            MachineConfig cfg = m0.machineConfig();
            cfg.probe.head_diameter = 0.0f;
            ConfigValidationError err = ConfigValidationError::Ok;
            if (!m0.trySetMachineConfig(cfg, err))
            {
                return 84;
            }
            const TestPoint disc0 = m0.machineConfig().probe.probe_offset_reference;
            ProbeCalibration c0;
            if (!c0.start(m0, disc0))
            {
                return 85;
            }
            for (int i = 0; i < 4000 && !c0.isComplete() && !c0.isError(); ++i)
            {
                m0.tick(0.05f);
                m0.setHostInputs(withDiscTrigger(m0, disc0));
                c0.tick(0.05f, m0);
            }
            if (!c0.isComplete())
            {
                return 86;
            }
            if (!nearEqual(m0.calibrationSet().probe_center.x, kept_center.x) ||
                !nearEqual(m0.calibrationSet().probe_center.y, kept_center.y) ||
                !nearEqual(m0.calibrationSet().probe_center.z, kept_center.z))
            {
                return 87;
            }
            if (!nearEqual(m0.calibrationSet().tool_offset.x, kept_offset.x) ||
                !nearEqual(m0.calibrationSet().tool_offset.y, kept_offset.y) ||
                !nearEqual(m0.calibrationSet().tool_offset.z, kept_offset.z))
            {
                return 88;
            }
        }

        // head_diameter > 0
        {
            Machine m2;
            if (!applyFastProbeConfig(m2))
            {
                return 89;
            }
            MachineConfig cfg = m2.machineConfig();
            cfg.probe.head_diameter = 5.0f;
            ConfigValidationError err = ConfigValidationError::Ok;
            if (!m2.trySetMachineConfig(cfg, err))
            {
                return 90;
            }
            const TestPoint disc2 = m2.machineConfig().probe.probe_offset_reference;
            ProbeCalibration c2;
            if (!c2.start(m2, disc2))
            {
                return 91;
            }
            for (int i = 0; i < 4000 && !c2.isComplete() && !c2.isError(); ++i)
            {
                m2.tick(0.05f);
                m2.setHostInputs(withDiscTrigger(m2, disc2));
                c2.tick(0.05f, m2);
            }
            if (!c2.isComplete())
            {
                return 92;
            }
            if (!nearEqual(m2.calibrationSet().probe_center.x, kept_center.x) ||
                !nearEqual(m2.calibrationSet().probe_center.y, kept_center.y) ||
                !nearEqual(m2.calibrationSet().probe_center.z, kept_center.z))
            {
                return 93;
            }
            if (!nearEqual(m2.calibrationSet().tool_offset.x, kept_offset.x) ||
                !nearEqual(m2.calibrationSet().tool_offset.y, kept_offset.y) ||
                !nearEqual(m2.calibrationSet().tool_offset.z, kept_offset.z))
            {
                return 94;
            }
        }
    }

    // Trigger pose must be the latched pose, not the later current TCP pose.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 90;
        }

        // Pick an intentionally different center so we can detect pose usage.
        const TestPoint expected_center{10.0f, 20.0f, 30.0f};
        const float r_sum = expectedXyContactRadius(m.machineConfig().probe.measuring_head_diameter,
                                                     m.machineConfig().probe.tool_diameter);
        const float thickness = m.machineConfig().probe.measuring_head_thickness;
        const float search_depth = m.machineConfig().probe.search_depth;

        const float tcp_z_top = expected_center.z + 0.5f * thickness;
        const float tcp_z_lat = expected_center.z - search_depth;

        const TestPoint nominal = m.machineConfig().probe.probe_offset_reference;
        const TestPoint expected_offset{nominal.x - expected_center.x, nominal.y - expected_center.y,
                                          nominal.z - expected_center.z};

        ProbeCalibration calib;
        const TestPoint search_ref{m.x(), m.y(), m.z()};
        if (!calib.start(m, search_ref))
        {
            return 91;
        }

        std::uint32_t seq = 1;
        for (int i = 0; i < 6000 && !calib.isComplete() && !calib.isError(); ++i)
        {
            const TestPoint tcp_to_latch = [&]() -> TestPoint {
                switch (calib.step())
                {
                case ProbeCalibration::Step::MeasureZ:
                    return TestPoint{nominal.x, nominal.y, tcp_z_top};
                case ProbeCalibration::Step::MeasureNegY:
                    return TestPoint{expected_center.x, expected_center.y - r_sum, tcp_z_lat};
                case ProbeCalibration::Step::MeasurePosY:
                    return TestPoint{expected_center.x, expected_center.y + r_sum, tcp_z_lat};
                case ProbeCalibration::Step::MeasureNegX:
                    return TestPoint{expected_center.x - r_sum, expected_center.y, tcp_z_lat};
                case ProbeCalibration::Step::MeasurePosX:
                    return TestPoint{expected_center.x + r_sum, expected_center.y, tcp_z_lat};
                default:
                    return TestPoint{nominal.x, nominal.y, nominal.z};
                }
            }();

            m.setHostInputs(withManualProbeTrigger(tcp_to_latch, true, seq++));
            m.tick(0.05f);
            calib.tick(0.05f, m);
        }

        if (!calib.isComplete())
        {
            return 92;
        }

        const TestPoint measured_center = m.calibrationSet().probe_center;
        if (!nearEqual(measured_center.x, expected_center.x) ||
            !nearEqual(measured_center.y, expected_center.y) ||
            !nearEqual(measured_center.z, expected_center.z))
        {
            return 93;
        }
        const TestPoint measured_offset = m.calibrationSet().tool_offset;
        if (!nearEqual(measured_offset.x, expected_offset.x) ||
            !nearEqual(measured_offset.y, expected_offset.y) ||
            !nearEqual(measured_offset.z, expected_offset.z))
        {
            return 94;
        }
    }

    // Architecture proof: disc center differs strongly from probe_offset_reference.
    // M19 must discover the real disc center via trigger contacts and must not search
    // around probe_offset_reference.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 200;
        }

        const TestPoint probe_ref = m.machineConfig().probe.probe_offset_reference;
        const TestPoint disc1{73.0f, 31.0f, 14.0f};
        const TestPoint start_tcp{72.0f, 30.0f, 20.0f};

        moveMachineTo(m, start_tcp);

        ProbeCalibration calib;
        const TestPoint search_ref{m.x(), m.y(), m.z()};
        if (!calib.start(m, search_ref) || !runCalib(calib, m, disc1, 8000))
        {
            return 201;
        }

        const TestPoint measured_center = m.calibrationSet().probe_center;
        if (!nearEqual(measured_center.x, disc1.x) || !nearEqual(measured_center.y, disc1.y) ||
            !nearEqual(measured_center.z, disc1.z, 1.0f))
        {
            return 202;
        }

        // tool_offset definition must stay unchanged:
        // tool_offset = probe_offset_reference - measured_probe
        const TestPoint expected_offset{probe_ref.x - disc1.x, probe_ref.y - disc1.y, probe_ref.z - disc1.z};
        const TestPoint measured_offset = m.calibrationSet().tool_offset;
        if (!nearEqual(measured_offset.x, expected_offset.x, 1.0f) ||
            !nearEqual(measured_offset.y, expected_offset.y, 1.0f) ||
            !nearEqual(measured_offset.z, expected_offset.z, 1.0f))
        {
            return 203;
        }
    }

    // Multiple discs with the same core configuration:
    // Core must keep using the current search_reference and discover the new disc center.
    {
        Machine m;
        if (!applyFastProbeConfig(m))
        {
            return 210;
        }
        const TestPoint probe_ref = m.machineConfig().probe.probe_offset_reference;
        const TestPoint disc2{55.0f, 67.0f, 8.0f};
        // Pick a TCP z so ProbeSearch's Z end aligns with disc z.
        const float search_depth = m.machineConfig().probe.search_depth;
        const TestPoint start_tcp2{54.0f, 66.0f, disc2.z + search_depth};
        moveMachineTo(m, start_tcp2);

        ProbeCalibration calib2;
        const TestPoint search_ref2{m.x(), m.y(), m.z()};
        if (!calib2.start(m, search_ref2) || !runCalib(calib2, m, disc2, 8000))
        {
            return 211;
        }

        const TestPoint measured_center2 = m.calibrationSet().probe_center;
        if (!nearEqual(measured_center2.x, disc2.x) || !nearEqual(measured_center2.y, disc2.y) ||
            !nearEqual(measured_center2.z, disc2.z, 1.0f))
        {
            return 212;
        }

        const TestPoint expected_offset2{probe_ref.x - disc2.x, probe_ref.y - disc2.y, probe_ref.z - disc2.z};
        const TestPoint measured_offset2 = m.calibrationSet().tool_offset;
        if (!nearEqual(measured_offset2.x, expected_offset2.x, 1.0f) ||
            !nearEqual(measured_offset2.y, expected_offset2.y, 1.0f) ||
            !nearEqual(measured_offset2.z, expected_offset2.z, 1.0f))
        {
            return 213;
        }
    }

    // Trigger without a valid trigger pose must not complete calibration nor overwrite offsets.
    {
        Machine kept_machine;
        if (!applyFastProbeConfig(kept_machine))
        {
            return 100;
        }
        const TestPoint disc = kept_machine.machineConfig().probe.probe_offset_reference;
        moveMachineTo(kept_machine, disc);
        ProbeCalibration kept_calib;
        const TestPoint kept_search_ref{kept_machine.x(), kept_machine.y(), kept_machine.z()};
        if (!kept_calib.start(kept_machine, kept_search_ref))
        {
            return 101;
        }
        for (int i = 0; i < 4000 && !kept_calib.isComplete() && !kept_calib.isError(); ++i)
        {
            kept_machine.tick(0.05f);
            kept_machine.setHostInputs(withDiscTrigger(kept_machine, disc));
            kept_calib.tick(0.05f, kept_machine);
        }
        if (!kept_calib.isComplete())
        {
            return 102;
        }

        const TestPoint kept_offset = kept_machine.calibrationSet().tool_offset;
        const bool kept_center_valid = kept_machine.calibrationSet().probe_center_valid;

        Machine fail_machine = kept_machine;
        ProbeCalibration fail_calib;
        const TestPoint fail_search_ref{fail_machine.x(), fail_machine.y(), fail_machine.z()};
        if (!fail_calib.start(fail_machine, fail_search_ref))
        {
            return 103;
        }

        std::uint32_t seq = 1;
        for (int i = 0; i < 2000 && !fail_calib.isComplete() && !fail_calib.isError(); ++i)
        {
            // Pose invalid: production hardening must not fall back to current TCP.
            fail_machine.setHostInputs(withManualProbeTrigger(TestPoint{0, 0, 0}, false, seq++));
            fail_machine.tick(0.05f);
            fail_calib.tick(0.05f, fail_machine);
        }

        if (!fail_calib.isError())
        {
            return 104;
        }
        if (fail_machine.calibrationSet().probe_center_valid != kept_center_valid)
        {
            return 105;
        }
        const TestPoint fail_offset = fail_machine.calibrationSet().tool_offset;
        if (!nearEqual(fail_offset.x, kept_offset.x) || !nearEqual(fail_offset.y, kept_offset.y) ||
            !nearEqual(fail_offset.z, kept_offset.z))
        {
            return 106;
        }
    }

    return 0;
}
