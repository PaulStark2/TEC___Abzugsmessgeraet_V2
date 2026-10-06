#pragma once

#include "HostInputs.hpp"
#include "TestPoint.hpp"

#include <cstdint>

namespace simplant
{

constexpr float kDefaultProbePulseSeconds = 0.1f;
// Hold-to-apply failsafe: UI must refresh the injected TCP force within this window.
constexpr float kInjectedForceHoldSeconds = 0.4f;
// Plant-side sanity bound. Above the 500 N config typo brake so a configured
// hard limit can still be exceeded and ForceHardLimit stays testable.
constexpr float kMaxInjectedForceAbs_N = 600.0f;

// Trip points of the virtual limit switches [machine position units].
struct PlantLimits
{
    float x_min = 0.0f;
    float x_max = 100.0f;
    float y_min = 0.0f;
    float y_max = 100.0f;
    float z_min = 0.0f;
    float z_max = 50.0f;
};

struct ProbeDisc
{
    TestPoint center{40.0f, 40.0f, 10.0f};
    float diameter = 8.0f;
    float thickness = 3.0f;
    float tool_diameter = 2.5f;
    float tool_cylindrical_height = 15.0f;
};

struct PlantState
{
    PlantLimits limits;
    ProbeDisc probe;
    // Continuous plant/TCP pose used for visualization and limit sampling (raw).
    float tcp_x = 0.0f;
    float tcp_y = 0.0f;
    float tcp_z = 0.0f;
    bool limit_x_min = false;
    bool limit_x_max = false;
    bool limit_y_min = false;
    bool limit_y_max = false;
    bool limit_z_min = false;
    bool limit_z_max = false;
    bool probe_triggered = false;
    bool e_stop = false;
    float probe_pulse_seconds = kDefaultProbePulseSeconds;
    float probe_trigger_x = 0.0f;
    float probe_trigger_y = 0.0f;
    float probe_trigger_z = 0.0f;
    std::uint32_t probe_trigger_sequence = 0;
    float force_stiffness_n_per_mm = 5.0f;
    // Last accepted Sim-UI hold vector [N]. Zero when idle or after hold timeout.
    float injected_fx = 0.0f;
    float injected_fy = 0.0f;
    float injected_fz = 0.0f;
    // Independent plant latch of the Core fixture valve command (no HostInputs feedback).
    bool fixture_open = false;
};

class SimPlant
{
public:
    void setLimits(const PlantLimits& limits);
    PlantLimits limits() const;

    void setProbeDisc(const ProbeDisc& disc);
    ProbeDisc probeDisc() const;

    void setProbePulseSeconds(float seconds);
    void triggerProbe();

    void setEStop(bool active);
    bool eStop() const;

    // Simulation-only tensile stiffness [N/mm]. Applied by SimProtocolBridge during
    // InsertInitial / OptimizeInsert / TestOrAge (not by sample() itself).
    void setForceStiffnessNPerMm(float k);
    float forceStiffnessNPerMm() const;

    // Simulation-only TCP disturbance [N]. Rejects non-finite values and |Fi| > 200 N.
    // {0,0,0} clears immediately. Non-zero vectors arm the hold timeout.
    bool setInjectedForce(float fx, float fy, float fz);
    ForceVector injectedForce() const;

    // Core actuator output only: true = valve energized (holder open). Plant latches
    // its own pose; it does not write fixture state back into HostInputs.
    void setFixtureValve(bool extend);
    bool fixtureOpen() const;

    void advance(float dt);

    HostInputs sample(float x, float y, float z) const;
    PlantState state(float x, float y, float z) const;

private:
    bool geometryContact(float x, float y, float z) const;

    PlantLimits limits_{};
    ProbeDisc probe_{};
    float probe_pulse_seconds_ = kDefaultProbePulseSeconds;
    float probe_remaining_ = 0.0f;
    bool e_stop_ = false;
    float force_stiffness_n_per_mm_ = 5.0f; // default: 50 N within 10 mm insert search
    ForceVector injected_force_{};
    float injected_remaining_ = 0.0f;
    mutable std::uint32_t force_sample_sequence_ = 0;
    mutable bool prev_probe_contact_ = false;
    mutable std::uint32_t probe_trigger_sequence_ = 0;
    mutable TestPoint latched_trigger_pose_{};
    bool fixture_open_ = false;
};

} // namespace simplant
