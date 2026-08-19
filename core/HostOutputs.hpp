#pragma once

// HostOutputs is a pure data container for later Host/Hardware integration. // container only
struct HostOutputs
{
    // Spreizer: spreaderExtend is the electrical valve command.
    // spreaderExtended is the last confirmed timer latch, not an end-position sensor.
    bool spreaderExtend; // true = valve energized (extend)
    bool spreaderExtended; // last confirmed extend latch (unknown after SafeDeenergized)

    // Bauplattenhalter / Einspannung: command and deterministic stub feedback (M11).
    bool fixtureExtend; // true = Halter offen
    bool fixtureExtended; // stub mirrors fixtureExtend
};
