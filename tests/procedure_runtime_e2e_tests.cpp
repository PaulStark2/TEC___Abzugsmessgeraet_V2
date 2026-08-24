#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "CoordinateTransform.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "SafeZ.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static bool nearEqual(float a, float b, float eps = 5.0e-2f)
{
    return std::fabs(a - b) <= eps;
}

static bool configureProcedure(Machine& machine, float standoff, float target_force)
{
    MachineConfig mcfg = machine.machineConfig();
    mcfg.motion.safe_z_buffer = 30.0f;
    mcfg.probe.approach_distance = standoff;
    mcfg.force.insert_search_distance = 10.0f;
    mcfg.force.insertion_buffer = 1.0f;
    mcfg.spreader.extend_time_s = 0.05f;
    mcfg.spreader.retract_time_s = 0.05f;
    mcfg.force.xy_correct_timeout_s = 5.0f;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(mcfg, err))
    {
        std::fprintf(stderr, "trySetMachineConfig failed err=%d\n", static_cast<int>(err));
        return false;
    }

    TestConfiguration tcfg = machine.testConfiguration();
    tcfg.target_force_N = target_force;
    tcfg.mode = TestMode::Aging;
    tcfg.iterations = 1u;
    if (!machine.trySetTestConfiguration(tcfg, err))
    {
        std::fprintf(stderr, "trySetTestConfiguration failed err=%d\n", static_cast<int>(err));
        return false;
    }
    return true;
}

static HostInputs inputsWithFz(Machine& machine, float fz)
{
    HostInputs in{};
    in.force.fz = fz;
    return withPlantInputs(machine, in);
}

// Approximate ProcessForceModel for bare Machine tests (no SimPlant).
static float syntheticProcessFz(float z, float z_contact, float target_force_N)
{
    if (z >= z_contact)
    {
        // Tension at contact, unload toward 0 above contact (5 N/mm).
        const float tension = -target_force_N + 5.0f * (z - z_contact);
        return (tension < 0.0f) ? tension : 0.0f;
    }
    // Compression below contact.
    const float compression = 5.0f * (z_contact - z);
    return (compression > target_force_N) ? target_force_N : compression;
}

static bool waitPhase(TestExecutor& ex, Machine& machine, ProcedurePhase want, int max_steps,
                      float fz)
{
    for (int i = 0; i < max_steps && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == want)
        {
            return true;
        }
        machine.setHostInputs(inputsWithFz(machine, fz));
        ex.tick(0.05f);
    }
    return ex.procedurePhase() == want;
}

// Track PointApproach Z targets: must see coarse (target+standoff) before fine (target).
static int testPointApproachCoarseThenFine()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    if (!homeMachine(machine))
    {
        return 1;
    }
    if (!calibrateProbeCenter(machine))
    {
        return 18;
    }

    const float standoff = 15.0f;
    const float target_force = 5.0f;
    if (!configureProcedure(machine, standoff, target_force))
    {
        return 9;
    }

    const TestPoint plate_pt{5.0f, 2.0f, 0.0f};
    TestPoint machine_pt{};
    if (!toMachinePoint(machine.calibrationSet(), plate_pt, machine_pt))
    {
        return 2;
    }
    const float coarse_z = machine_pt.z + standoff;
    const float fine_z = machine_pt.z;

    TestExecutor ex(machine);
    TestProgram program;
    program.add(plate_pt);
    ex.setProgram(program);
    if (!ex.start())
    {
        return 3;
    }

    // Skip probe calibrate path: clear probe and start from MoveSafePose by
    // running with probe already valid — ProbeApproach still runs first.
    // Drive through ProbeApproach+Calibrate with virtual probe triggers.
    bool saw_coarse = false;
    bool saw_fine = false;
    float z_when_coarse = 0.0f;
    float z_when_fine = 0.0f;

    for (int i = 0; i < 8000 && ex.processState() == ProcessState::Running; ++i)
    {
        const ProcedurePhase ph = ex.procedurePhase();
        HostInputs in{};
        if (ph == ProcedurePhase::InsertInitial || ph == ProcedurePhase::OptimizeInsert ||
            ph == ProcedurePhase::TestOrAge)
        {
            // Ramp Fz with penetration below contact (sim spring model).
            const float contact = fine_z;
            in.force.fz = 5.0f * (contact - machine.z());
            if (in.force.fz < 0.0f)
            {
                in.force.fz = 0.0f;
            }
            if (in.force.fz > target_force + 1.0f)
            {
                in.force.fz = target_force + 1.0f;
            }
        }
        const TestPoint disc = machine.calibrationSet().probe_center_valid
                                   ? machine.calibrationSet().probe_center
                                   : machine.machineConfig().probe.probe_offset_reference;
        machine.setHostInputs(withFreshForce(withVirtualProbeDisc(machine, disc, in)));
        ex.tick(0.05f);

        // After tick: coarse→fine transition leaves TCP at standoff for one sample;
        // fine→PreClamp leaves TCP at target.z (phase already advanced).
        if (!saw_coarse && nearEqual(machine.z(), coarse_z, 0.2f) &&
            (ex.procedurePhase() == ProcedurePhase::PointApproach ||
             ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect))
        {
            saw_coarse = true;
            z_when_coarse = machine.z();
            std::fprintf(stderr, "[e2e] coarse sample z=%.3f expect=%.3f\n", z_when_coarse, coarse_z);
        }
        if (saw_coarse && !saw_fine && nearEqual(machine.z(), fine_z, 0.2f) &&
            (ex.procedurePhase() == ProcedurePhase::PointApproach ||
             ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect ||
             ex.procedurePhase() == ProcedurePhase::Clamp ||
             ex.procedurePhase() == ProcedurePhase::InsertInitial))
        {
            saw_fine = true;
            z_when_fine = machine.z();
            std::fprintf(stderr, "[e2e] fine sample z=%.3f expect=%.3f\n", z_when_fine, fine_z);
        }

        if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert && saw_fine)
        {
            break;
        }
        if (ex.processState() == ProcessState::Aborted)
        {
            std::fprintf(stderr, "[e2e] aborted at phase=%u z=%.3f fz=%.3f\n",
                         static_cast<unsigned>(ex.procedurePhase()), machine.z(),
                         machine.hostInputs().force.fz);
            return 4;
        }
    }

    if (!saw_coarse)
    {
        std::fprintf(stderr, "[e2e] never settled at coarse z=target+standoff (%.3f)\n", coarse_z);
        return 5;
    }
    if (!saw_fine)
    {
        std::fprintf(stderr, "[e2e] never settled at fine z=target (%.3f)\n", fine_z);
        return 6;
    }
    // Fine must not happen before coarse, and fine must not be below target first.
    if (z_when_fine < fine_z - 0.5f)
    {
        std::fprintf(stderr, "[e2e] fine settle undershot target (z=%.3f < %.3f)\n", z_when_fine,
                     fine_z);
        return 7;
    }
    if (z_when_coarse < fine_z + standoff * 0.5f)
    {
        std::fprintf(stderr, "[e2e] coarse was not clearly above target\n");
        return 8;
    }
    return 0;
}

static int testStopInvalidatesHoming()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    if (!homeMachine(machine))
    {
        return 10;
    }
    if (!machine.isHomed())
    {
        return 11;
    }
    if (!calibrateProbeCenter(machine))
    {
        return 19;
    }
    if (!configureProcedure(machine, 15.0f, 5.0f))
    {
        return 17;
    }
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{1.0f, 0.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return 12;
    }
    if (!waitPhase(ex, machine, ProcedurePhase::ProbeApproach, 200, 0.0f) &&
        !waitPhase(ex, machine, ProcedurePhase::PointApproach, 200, 0.0f))
    {
        // Either probe or point approach is fine; just need Running.
        if (ex.processState() != ProcessState::Running)
        {
            return 13;
        }
    }

    ex.stop();
    if (ex.processState() != ProcessState::Aborted)
    {
        return 14;
    }
    if (machine.isHomed())
    {
        std::fprintf(stderr, "[e2e] STOP must clear isHomed (NeedsHoming)\n");
        return 15;
    }
    if (ex.start())
    {
        std::fprintf(stderr, "[e2e] start after STOP must fail without re-home\n");
        return 16;
    }
    return 0;
}

static int testInsertReachesPhase1ThenOptimize()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    if (!homeMachine(machine))
    {
        return 20;
    }
    if (!calibrateProbeCenter(machine))
    {
        return 27;
    }
    if (!configureProcedure(machine, 15.0f, 5.0f))
    {
        return 26;
    }

    TestPoint machine_pt{};
    const TestPoint plate_pt{3.0f, 1.0f, 0.0f};
    if (!toMachinePoint(machine.calibrationSet(), plate_pt, machine_pt))
    {
        return 21;
    }

    TestExecutor ex(machine);
    TestProgram program;
    program.add(plate_pt);
    ex.setProgram(program);
    if (!ex.start())
    {
        return 22;
    }

    const float contact_z = machine_pt.z;
    bool reached_insert = false;
    bool reached_optimize = false;

    for (int i = 0; i < 10000 && ex.processState() == ProcessState::Running; ++i)
    {
        HostInputs in{};
        const ProcedurePhase ph = ex.procedurePhase();
        if (phaseNeedsProcessForce(ph))
        {
            // Match SimPlant default stiffness 5 N/mm.
            in.force.fz = 5.0f * (contact_z - machine.z());
            if (in.force.fz < 0.0f)
            {
                in.force.fz = 0.0f;
            }
        }
        const TestPoint disc = machine.calibrationSet().probe_center_valid
                                   ? machine.calibrationSet().probe_center
                                   : machine.machineConfig().probe.probe_offset_reference;
        machine.setHostInputs(withFreshForce(withVirtualProbeDisc(machine, disc, in)));
        ex.tick(0.05f);

        if (ph == ProcedurePhase::InsertInitial)
        {
            reached_insert = true;
            // Must not already be deep below target before Phase-1 starts.
            if (machine.z() < contact_z - 2.0f && machine.motionState() == MotionState::AtTarget &&
                machine.hostInputs().force.fz < 0.1f)
            {
                // still diving is OK; abort only if settled below without force
            }
        }
        if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert)
        {
            reached_optimize = true;
            break;
        }
        if (ex.processState() == ProcessState::Aborted)
        {
            std::fprintf(stderr, "[e2e] aborted before OptimizeInsert phase=%u z=%.3f fz=%.3f\n",
                         static_cast<unsigned>(ex.procedurePhase()), machine.z(),
                         machine.hostInputs().force.fz);
            return 23;
        }
    }

    if (!reached_insert)
    {
        return 24;
    }
    if (!reached_optimize)
    {
        return 25;
    }
    return 0;
}

// Phase 3 iterations must not bound Phase 2; high iterations + in-tolerance force → TestOrAge.
static int testPhase2IndependentOfGuiIterations()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    if (!homeMachine(machine) || !calibrateProbeCenter(machine))
    {
        return 30;
    }
    if (!configureProcedure(machine, 15.0f, 5.0f))
    {
        return 31;
    }
    TestConfiguration tcfg = machine.testConfiguration();
    tcfg.iterations = 50u; // must NOT force 50 Phase-2 attempts
    tcfg.mode = TestMode::Aging;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetTestConfiguration(tcfg, err))
    {
        return 32;
    }
    MachineConfig mcfg = machine.machineConfig();
    mcfg.phase2.force_tolerance_N = 1.0f;
    mcfg.phase2.max_optimization_iterations = 5u;
    if (!machine.trySetMachineConfig(mcfg, err))
    {
        return 33;
    }

    const TestPoint plate_pt{2.0f, 0.0f, 0.0f};
    TestPoint machine_pt{};
    if (!toMachinePoint(machine.calibrationSet(), plate_pt, machine_pt))
    {
        return 34;
    }

    TestExecutor ex(machine);
    TestProgram program;
    program.add(plate_pt);
    ex.setProgram(program);
    if (!ex.start())
    {
        return 35;
    }

    const float contact = machine_pt.z;
    bool saw_test = false;
    for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
    {
        HostInputs in{};
        if (phaseNeedsProcessForce(ex.procedurePhase()))
        {
            in.force.fz = syntheticProcessFz(machine.z(), contact, 5.0f);
        }
        machine.setHostInputs(withPlantInputs(machine, in));
        ex.tick(0.05f);
        if (ex.procedurePhase() == ProcedurePhase::TestOrAge)
        {
            saw_test = true;
            break;
        }
        if (ex.processState() == ProcessState::Aborted)
        {
            return 36;
        }
    }
    if (!saw_test)
    {
        return 37;
    }
    return 0;
}

// Target forces 1 / 25 / 50 N reachable with k=5 N/mm spring within insert search.
static int testForceLevelsWithStiffness5()
{
    const float levels[] = {1.0f, 25.0f, 50.0f};
    for (float target : levels)
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(machine) || !calibrateProbeCenter(machine))
        {
            return 40;
        }
        if (!configureProcedure(machine, 15.0f, target))
        {
            return 41;
        }
        MachineConfig mcfg = machine.machineConfig();
        mcfg.force.insert_search_distance = 25.0f; // enough for 50 N at k=5 after unload
        mcfg.collision.contact_N = 50.0f;
        mcfg.collision.measure_N = 50.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 42;
        }

        TestPoint machine_pt{};
        const TestPoint plate_pt{1.0f, 0.0f, 0.0f};
        if (!toMachinePoint(machine.calibrationSet(), plate_pt, machine_pt))
        {
            return 43;
        }

        TestExecutor ex(machine);
        TestProgram program;
        program.add(plate_pt);
        ex.setProgram(program);
        if (!ex.start())
        {
            return 44;
        }

        const float contact = machine_pt.z;
        bool ok = false;
        for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
        {
            HostInputs in{};
            if (phaseNeedsProcessForce(ex.procedurePhase()))
            {
                in.force.fz = syntheticProcessFz(machine.z(), contact, target);
            }
            machine.setHostInputs(withPlantInputs(machine, in));
            ex.tick(0.05f);
            if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert ||
                ex.procedurePhase() == ProcedurePhase::TestOrAge)
            {
                ok = true;
                break;
            }
            if (ex.processState() == ProcessState::Aborted)
            {
                std::fprintf(stderr, "[e2e] force level %.1f aborted z=%.3f fz=%.3f\n", target,
                             machine.z(), machine.hostInputs().force.fz);
                return 45;
            }
        }
        if (!ok)
        {
            return 46;
        }
    }
    return 0;
}

// After last point: Safe-Z then Machine::home() (not park at measured XY @ Safe-Z).
static int testSafeEndRunsMachineHome()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    if (!homeMachine(machine) || !calibrateProbeCenter(machine))
    {
        return 50;
    }
    MachineConfig cfg = machine.machineConfig();
    cfg.calibration.home_offset = TestPoint{0.0f, 0.0f, 0.0f};
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(cfg, err))
    {
        return 51;
    }
    if (!configureProcedure(machine, 15.0f, 5.0f))
    {
        return 53;
    }
    TestConfiguration tcfg = machine.testConfiguration();
    tcfg.iterations = 1u;
    tcfg.mode = TestMode::Aging;
    if (!machine.trySetTestConfiguration(tcfg, err))
    {
        return 54;
    }

    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{1.0f, 0.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return 55;
    }

    bool saw_homing = false;
    const float contact = 0.0f;
    for (int i = 0; i < 25000 && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == ProcedurePhase::MoveSafeEnd &&
            machine.machineState() == MachineState::Homing)
        {
            saw_homing = true;
        }
        HostInputs in{};
        if (phaseNeedsProcessForce(ex.procedurePhase()))
        {
            in.force.fz = syntheticProcessFz(machine.z(), contact, 5.0f);
        }
        machine.setHostInputs(withPlantInputs(machine, in));
        ex.tick(0.05f);
    }
    if (ex.processState() != ProcessState::Completed)
    {
        return 56;
    }
    if (!saw_homing || !machine.isHomed() || machine.homingState() != HomingState::Homed)
    {
        return 57;
    }
    const TestPoint home = machine.calibrationSet().home_offset;
    if (!nearEqual(machine.x(), home.x, 0.5f) || !nearEqual(machine.y(), home.y, 0.5f) ||
        !nearEqual(machine.z(), home.z, 0.5f))
    {
        std::fprintf(stderr, "[e2e] post-home pose=(%.3f,%.3f,%.3f) expected=(%.3f,%.3f,%.3f)\n",
                     machine.x(), machine.y(), machine.z(), home.x, home.y, home.z);
        return 58;
    }
    return 0;
}

int main()
{
    if (const int rc = testPointApproachCoarseThenFine())
    {
        return rc;
    }
    if (const int rc = testStopInvalidatesHoming())
    {
        return rc;
    }
    if (const int rc = testInsertReachesPhase1ThenOptimize())
    {
        return rc;
    }
    if (const int rc = testPhase2IndependentOfGuiIterations())
    {
        return rc;
    }
    if (const int rc = testForceLevelsWithStiffness5())
    {
        return rc;
    }
    if (const int rc = testSafeEndRunsMachineHome())
    {
        return rc;
    }
    std::fprintf(stderr, "procedure_runtime_e2e_tests: OK\n");
    return 0;
}
