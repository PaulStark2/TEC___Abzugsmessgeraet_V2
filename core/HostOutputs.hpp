#pragma once

// HostOutputs is a pure data container for later Host/Hardware integration. // container only
struct HostOutputs
{
    // Spreizer: one logical command. Firmware maps it to two valves (PC8 open / PC12 close).
    // spreaderExtended is the last confirmed timer latch, not an end-position sensor.
    bool spreaderExtend; // true = open pair, false = close pair (frozen in SafeDeenergized)
    bool spreaderExtended; // last confirmed extend latch (unknown after SafeDeenergized)

    // Bauplattenhalter / Einspannung: command and deterministic stub feedback (M11).
    bool fixtureExtend; // true = Halter offen
    bool fixtureExtended; // stub mirrors fixtureExtend
};
