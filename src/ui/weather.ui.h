#pragma once

#include <cstdint>

#include "dsp/engine.h"
#include "ui/layout.h"
#include "ui/panel.h"

namespace tw {

/*
Turns the panel into the instrument.

The eight analog inputs go straight to Params. The pads latch drone notes: tap
one and it swells in and keeps sounding; tap it again and it fades. Switch A
picks the scale, switch B the register, and shift+pad the root, and held notes
glide to the new tuning rather than jumping.

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

private:
    void _apply_tuning(const PanelInput& in);
    void _pad(int role, bool shift);
    void _ghosts(float dt, float motion);
    void _push();

    static constexpr int kMaxGhosts = 2;

    Engine& _engine;
    Tuning _tuning;
    uint16_t _user { 0 }, _ghost { 0 };
    uint16_t _prev_seen { 0 };
    float _blink { 0 };
    float _ghost_clock { 40.f };
    float _ghost_life[Drone::kDegrees] {};
    Rng _rng { 77 };
};

}
