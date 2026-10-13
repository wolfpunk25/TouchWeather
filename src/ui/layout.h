#pragma once

#include <cstdint>

namespace tw {

/*
Which of the Touch 2's twelve pads does what.

Pads 0-9 are the ten notes of the drone, running up two octaves of whichever
scale switch A has chosen. Pad 10 lets go of every note. Pad 11 is shift: hold
it and tap pads 0-10 to set the root (C, C#, D ... A#).

The numbers on the right are MPR121 electrode indices (P00..P11) and are the one
thing here that has to match the faceplate in use. They start as the identity
map. Run a DEBUG build (make DEBUG=1) and the serial log prints every pad as it
is touched, which is how to find out which electrode is which; then edit kPad.
*/
namespace layout {

constexpr uint8_t kNotePads = 10;
constexpr uint8_t kRoleClear = 10;
constexpr uint8_t kRoleShift = 11;
constexpr uint8_t kRoles = 12;

/* Role -> electrode. */
constexpr uint8_t kPad[kRoles] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

/* Flip any control whose pot is wired so that clockwise reads lower. */
constexpr bool kInvert[8] = { false, false, false, false, false, false, false, false };

} // namespace layout
}
