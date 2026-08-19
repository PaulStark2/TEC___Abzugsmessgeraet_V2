#include "SimPlant.hpp"

#include "ProbeGeometry.hpp"

namespace simplant
{

void SimPlant::setLimits(const PlantLimits& limits)
{
    limits_ = limits;
}

PlantLimits SimPlant::limits() const
{
    return limits_;
}

void SimPlant::setProbeDisc(const ProbeDisc& disc)
{
    probe_ = disc;
}

ProbeDisc SimPlant::probeDisc() const
{
    return probe_;
}

void SimPlant::setProbePulseSeconds(float seconds)
{
    if (seconds > 0.0f)
    {
        probe_pulse_seconds_ = seconds;
    }
}

void SimPlant::triggerProbe()
{
    probe_remaining_ = probe_pulse_seconds_;
}

void SimPlant::setEStop(bool active)
{
    e_stop_ = active;
}

bool SimPlant::eStop() const
{
    return e_stop_;
}

void SimPlant::advance(float dt)
{
    if (dt <= 0.0f || probe_remaining_ <= 0.0f)
    {
        return;
    }

    probe_remaining_ -= dt;
    if (probe_remaining_ < 0.0f)
    {
        probe_remaining_ = 0.0f;
    }
}

bool SimPlant::geometryContact(float x, float y, float z) const
{
    return toolContactsProbeDisc(x, y, z, probe_.center.x, probe_.center.y, probe_.center.z,
                                 probe_.diameter, probe_.thickness, probe_.tool_diameter,
                                 probe_.tool_cylindrical_height);
}

HostInputs SimPlant::sample(float x, float y, float z) const
{
    HostInputs inputs{};

    inputs.force = ForceVector{0.0f, 0.0f, 0.0f};

    inputs.limitXMin = x <= limits_.x_min;
    inputs.limitXMax = x >= limits_.x_max;
    inputs.limitYMin = y <= limits_.y_min;
    inputs.limitYMax = y >= limits_.y_max;
    inputs.limitZMin = z <= limits_.z_min;
    inputs.limitZMax = z >= limits_.z_max;

    inputs.e_stop = e_stop_;
    const bool contact = (probe_remaining_ > 0.0f) || geometryContact(x, y, z);
    if (contact && !prev_probe_contact_)
    {
        ++probe_trigger_sequence_;
        latched_trigger_pose_ = TestPoint{x, y, z};
    }
    prev_probe_contact_ = contact;
    inputs.probeTriggered = contact;
    if (contact)
    {
        inputs.probeTriggerPoseValid = true;
        inputs.probeTriggerX = latched_trigger_pose_.x;
        inputs.probeTriggerY = latched_trigger_pose_.y;
        inputs.probeTriggerZ = latched_trigger_pose_.z;
        inputs.probeTriggerSequence = probe_trigger_sequence_;
    }
    inputs.forceSampleValid = true;
    inputs.forceSampleSequence = ++force_sample_sequence_;
    inputs.hostHeartbeatValid = true;
    inputs.hostHeartbeatSequence = inputs.forceSampleSequence;

    return inputs;
}

PlantState SimPlant::state(float x, float y, float z) const
{
    const HostInputs inputs = sample(x, y, z);

    PlantState snapshot{};
    snapshot.limits = limits_;
    snapshot.probe = probe_;
    snapshot.limit_x_min = inputs.limitXMin;
    snapshot.limit_x_max = inputs.limitXMax;
    snapshot.limit_y_min = inputs.limitYMin;
    snapshot.limit_y_max = inputs.limitYMax;
    snapshot.limit_z_min = inputs.limitZMin;
    snapshot.limit_z_max = inputs.limitZMax;
    snapshot.probe_triggered = inputs.probeTriggered;
    snapshot.e_stop = inputs.e_stop;
    snapshot.probe_pulse_seconds = probe_pulse_seconds_;
    snapshot.probe_trigger_x = inputs.probeTriggerX;
    snapshot.probe_trigger_y = inputs.probeTriggerY;
    snapshot.probe_trigger_z = inputs.probeTriggerZ;
    snapshot.probe_trigger_sequence = inputs.probeTriggerSequence;
    return snapshot;
}

} // namespace simplant
