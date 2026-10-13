#pragma once

#include <cstdint>

namespace tw {

/*
The Touch 2 has twelve pads, MPR121 electrodes P00-P11 (numbered on the board's
silkscreen, and checked against it with the DEBUG log):

    P00-P09   ten drone notes, tap to latch, tap again to release
    P10       root down a semitone   (the small pad at the top left)
    P11       root up a semitone     (the small pad at the top right)

P10 and P11 held together for about half a second release every note. Touched
together they cancel out, so the root does not move.

The numbers on the right are electrode indices, one per role, in case a faceplate
ever wires them differently.
*/
namespace layout {

constexpr uint8_t kNotePads = 10;
constexpr uint8_t kRoleRootDown = 10;
constexpr uint8_t kRoleRootUp = 11;
constexpr uint8_t kRoles = 12;

/* Role -> electrode. */
constexpr uint8_t kPad[kRoles] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

/* How long to hold the two top pads to clear everything. */
constexpr float kClearHold = .6f;

/* Flip any control whose pot is wired so that clockwise reads lower. */
constexpr bool kInvert[8] = { false, false, false, false, false, false, false, false };

} // namespace layout
}
