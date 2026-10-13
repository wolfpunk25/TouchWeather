#pragma once

#include <cstdint>

#include "dsp/engine.h"
#include "ui/layout.h"
#include "ui/panel.h"

namespace tw {

/*
Turns the panel into the instrument.

The eight analog inputs go straight to Params. The ten note pads latch
drone notes: tap one and it swells in and keeps sounding; tap it again and it
fades. Only two notes sound at a time: a third tap releases the oldest. Switch A
picks the scale; switch B picks the octave of the *next* note latched (notes
already sounding keep theirs). The two small pads at the top step the root down
and up a semitone, and held together clear everything (see ui/layout.h). Held
notes glide to a new root or scale rather than jumping.

With Motion up, the UI also plays a little by itself: now and then it lets a
ghost note into the drone, and takes it out again half a minute later. Ghosts
never touch a note you latched, and tapping one adopts it as yours (or, if
you'd rather, fades it out).
*/
class WeatherUI {
public:
    explicit WeatherUI(Engine& engine) : _engine(engine) {}

    /* Adopt the panel as it is, and start with the root and fifth sounding. */
    void init(const PanelInput& in);

    /* Once per audio block; dt is the block's length in seconds. */
    void poll(const PanelInput& in, float dt);

    /* The Seed's LED: lightning, breaking waves, and a blink for each pad. */
    float led() const;

    uint16_t latched() const { return _user; }
    uint16_t ghosts() const { return _ghost; }
    const Tuning& tuning() const { return _tuning; }
    int octave_of(const int degree) const { return _oct[degree]; }
    int next_octave() const { return _oct_next; }

    /* At most this many notes latched by hand at once; the oldest makes way. */
    static constexpr int kMaxNotes = 2;

private:
    void _apply_tuning(const PanelInput& in);
    void _pads(const PanelInput& in, float dt);
    void _tap(int role);
    void _ghosts(float dt, float motion);
    void _push();
    void _latch(int role);

    static constexpr int kMaxGhosts = 2;

    Engine& _engine;
    Tuning _tuning;
    uint16_t _user { 0 }, _ghost { 0 };
    int8_t _oct[Drone::kDegrees] {};           // the octave each note was played in
    int8_t _oct_next { 0 };                    // switch B: where the next note goes
    uint32_t _stamp[Drone::kDegrees] {};       // when each user note was latched
    uint32_t _clock { 0 };
    uint16_t _down { 0 };                 // roles held at the last poll
    float _clear_t { 0 };
    bool _cleared { false };
    float _blink { 0 };
    float _ghost_clock { 40.f };
    float _ghost_life[Drone::kDegrees] {};
    Rng _rng { 77 };
};

}
