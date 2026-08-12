#pragma once

// HostOutputs is a pure data container for later Host/Hardware integration. // container only
struct HostOutputs
{
    // Spreizer / Greifwerkzeug: command and deterministic stub feedback (M11).
    bool spreaderExtend; // true = Spreizer aktiv
    bool spreaderExtended; // stub mirrors spreaderExtend

    // Bauplattenhalter / Einspannung: command and deterministic stub feedback (M11).
    bool fixtureExtend; // true = Halter offen
    bool fixtureExtended; // stub mirrors fixtureExtend
};
