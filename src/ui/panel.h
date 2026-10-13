#pragma once

#include <array>
#include <cstdint>

namespace tw {

/*
What the Synthux Touch 2 front panel looks like to the UI: a plain snapshot,
so the same UI code runs against the real board and against the host tests.

Knobs and faders are normalised 0..1 and already oriented so that clockwise /
up is larger. The switches are Daisy's Switch3 positions.
*/
struct PanelInput {
    /* The eight analog inputs, in the order they sit on A0..A7. */
    enum Knob: uint8_t {
        KnobWind,       // A0
        KnobRain,       // A1
        KnobThunder,    // A2
        KnobOcean,      // A3
        KnobDrone,      // A4
        KnobMotion,     // A5
        FaderReverb,    // A6 - left fader
        FaderVolume,    // A7 - right fader
        KnobCount
    };

    /* Switch3 positions. */
    enum Switch: uint8_t { SwCentre = 0, SwUp = 1, SwDown = 2 };

    std::array<float, KnobCount> knob {};
    std::array<uint8_t, 2> sw {};     // [0] switch A, [1] switch B

    /* Pads held right now, bit n = MPR121 electrode n. */
    uint16_t pads { 0 };

    /* Pads touched at any moment since the previous snapshot was consumed.
       The board is polled more slowly than the UI runs, and a brisk tap can
       begin and end between two polls; this keeps it from vanishing. */
    uint16_t pads_seen { 0 };

    /* False if the touch controller has stopped answering. */
    bool pads_ok { true };
};

}
