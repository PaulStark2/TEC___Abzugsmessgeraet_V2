#include "HostInputs.hpp"
#include "Machine.hpp"
#include "ProbeCalibration.hpp"

int main()
{
    // Placeholder completes at search end when the probe never fires.
    {
        Machine m;
        ProbeCalibration calib;
        if (!calib.start(m))
        {
            return 1;
        }
        if (calib.state() != ProbeCalibration::State::Running)
        {
            return 2;
        }
        HostInputs in{};
        m.setHostInputs(in);
        for (int i = 0; i < 500 && !calib.isComplete(); ++i)
        {
            m.tick(0.05f);
            calib.tick(0.05f, m);
        }
        if (!calib.isComplete() || calib.isError())
        {
            return 3;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 4;
        }
    }

    // Trigger records the pose and overwrites ProbeCenter.
    {
        Machine m;
        ProbeCalibration calib;
        calib.start(m);
        HostInputs in{};
        m.setHostInputs(in);
        m.tick(0.05f);
        in.probeTriggered = true;
        m.setHostInputs(in);
        m.tick(0.05f);
        calib.tick(0.05f, m);
        if (!calib.isComplete())
        {
            return 10;
        }
        const TestPoint first = calib.result();
        ProbeCalibration calib2;
        calib2.start(m);
        m.tick(0.05f);
        in.probeTriggered = true;
        m.setHostInputs(in);
        m.tick(0.05f);
        calib2.tick(0.05f, m);
        if (!m.calibrationSet().probe_center_valid)
        {
            return 11;
        }
        (void)first;
    }

    // Fault during search is an error.
    {
        Machine m;
        ProbeCalibration calib;
        calib.start(m);
        HostInputs in{};
        in.e_stop = true;
        m.setHostInputs(in);
        m.tick(0.05f);
        calib.tick(0.05f, m);
        if (!calib.isError())
        {
            return 12;
        }
    }

    return 0;
}
