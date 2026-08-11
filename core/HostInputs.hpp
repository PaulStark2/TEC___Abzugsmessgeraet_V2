#pragma once

// HostInputs is a pure data container for later Host/Hardware integration. // container only
struct HostInputs
{
    // Example force reading from the force sensor (units are defined later). // placeholder field
    float force; // force value placeholder

    // Limit switch flags (evaluated later, not in M5). // placeholder limits
    bool limitXMin; // X minimum limit
    bool limitXMax; // X maximum limit
    bool limitYMin; // Y minimum limit
    bool limitYMax; // Y maximum limit
    bool limitZMin; // Z minimum limit
    bool limitZMax; // Z maximum limit

    // Emergency stop flag (not evaluated in M5). // placeholder E-Stop
    bool e_stop; // e-stop state placeholder
};

