#include "ui/weather.ui.h"

#include <algorithm>

using namespace tw;

void WeatherUI::init(const PanelInput& in)
{
    _apply_tuning(in);
    _user = (1u << 0) | (1u << 3);   // root and fifth: an open drone out of the box
    _stamp[0] = ++_clock;
    _stamp[3] = ++_clock;
    _down = 0;
    _push();
}

void WeatherUI::_apply_tuning(const PanelInput& in)
{
    // Switch A: up = bright, centre = dark, down = suspended.
    switch (in.sw[0]) {
        case PanelInput::SwUp:   _tuning.scale = 0; break;
        case PanelInput::SwDown: _tuning.scale = 2; break;
        default:                 _tuning.scale = 1; break;
    }
    // Switch B: the octave for the next note.
    switch (in.sw[1]) {
        case PanelInput::SwUp:   _oct_next = 1; break;
        case PanelInput::SwDown: _oct_next = -1; break;
        default:                 _oct_next = 0; break;
    }
}

void WeatherUI::_push()
{
    _engine.drone().set(_user | _ghost, _tuning, _oct);
}

void WeatherUI::_tap(const int role)
{
    _blink = .8f;
    const uint16_t bit = 1u << role;
    if (_ghost & bit) _ghost &= ~bit;   // tapping a ghost sends it away
    else if (_user & bit) _user &= ~bit;
    else _latch(role);
    _push();
}

void WeatherUI::_latch(const int role)
{
    _user |= 1u << role;
    _oct[role] = _oct_next;
    _stamp[role] = ++_clock;

    // Two at a time: the oldest note gives way to the newest.
    while (__builtin_popcount(_user) > kMaxNotes) {
        int oldest = -1;
        for (int d = 0; d < Drone::kDegrees; d++) {
            if (!(_user & (1u << d)) || d == role) continue;
            if (oldest < 0 || _stamp[d] < _stamp[oldest]) oldest = d;
        }
        _user &= ~(1u << oldest);
    }
}

void WeatherUI::_pads(const PanelInput& in, const float dt)
{
    uint16_t held = 0, seen = 0;
    for (int r = 0; r < layout::kRoles; r++) {
        const uint16_t e = 1u << layout::kPad[r];
        if (in.pads & e) held |= 1u << r;
        if ((in.pads | in.pads_seen) & e) seen |= 1u << r;
    }
    const uint16_t rising = seen & ~_down;

    for (int r = 0; r < layout::kNotePads; r++) {
        if (rising & (1u << r)) _tap(r);
    }
    if (rising & (1u << layout::kRoleRootDown)) { _tuning.root = (_tuning.root + 11) % 12; _blink = .8f; _push(); }
    if (rising & (1u << layout::kRoleRootUp))   { _tuning.root = (_tuning.root + 1) % 12;  _blink = .8f; _push(); }

    // Both root pads, held: let everything go.
    constexpr uint16_t kBoth = (1u << layout::kRoleRootDown) | (1u << layout::kRoleRootUp);
    if ((held & kBoth) == kBoth) {
        _clear_t += dt;
        if (_clear_t >= layout::kClearHold && !_cleared) {
            _user = 0;
            _ghost = 0;
            _cleared = true;
            _blink = 1.f;
            _push();
        }
    }
    else {
        _clear_t = 0.f;
        _cleared = false;
    }
    _down = held;
}

void WeatherUI::_ghosts(const float dt, const float motion)
{
    bool changed = false;

    for (int d = 0; d < Drone::kDegrees; d++) {
        if (!(_ghost & (1u << d))) continue;
        _ghost_life[d] -= dt;
        // Switching motion down sends them home too.
        if (_ghost_life[d] <= 0.f || motion < .4f) { _ghost &= ~(1u << d); changed = true; }
    }

    if (motion >= .4f) {
        _ghost_clock -= dt;
        if (_ghost_clock <= 0.f) {
            _ghost_clock = _rng.range(25.f, 70.f) / (.5f + motion);
            int count = 0;
            for (int d = 0; d < Drone::kDegrees; d++) if (_ghost & (1u << d)) count++;
            const uint16_t taken = _user | _ghost;
            int free_notes = 0;
            for (int d = 0; d < Drone::kDegrees; d++) if (!(taken & (1u << d))) free_notes++;
            if (count < kMaxGhosts && free_notes > 0 && __builtin_popcount(taken) < 6) {
                int pick = static_cast<int>(_rng.uni() * free_notes) % free_notes;
                for (int d = 0; d < Drone::kDegrees; d++) {
                    if (taken & (1u << d)) continue;
                    if (pick-- == 0) {
                        _ghost |= 1u << d;
                        _oct[d] = _oct_next;
                        _ghost_life[d] = _rng.range(30.f, 80.f);
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    if (changed) _push();
}

void WeatherUI::poll(const PanelInput& in, const float dt)
{
    Params p;
    p.wind = in.knob[PanelInput::KnobWind];
    p.rain = in.knob[PanelInput::KnobRain];
    p.thunder = in.knob[PanelInput::KnobThunder];
    p.ocean = in.knob[PanelInput::KnobOcean];
    p.drone = in.knob[PanelInput::KnobDrone];
    p.motion = in.knob[PanelInput::KnobMotion];
    p.reverb = in.knob[PanelInput::FaderReverb];
    p.volume = in.knob[PanelInput::FaderVolume];
    _engine.set(p);

    const Tuning before = _tuning;
    const int root = _tuning.root;
    _apply_tuning(in);
    _tuning.root = root;

    bool dirty = _tuning.scale != before.scale;

    if (in.pads_ok) _pads(in, dt);
    else { _down = 0; _clear_t = 0.f; _cleared = false; }

    if (dirty) _push();
    _ghosts(dt, p.motion);

    _blink = std::max(0.f, _blink - dt / .12f);
}

float WeatherUI::led() const
{
    // A faint glow that swells with the surf, so it is plainly alive.
    const float glow = .03f + .10f * _engine.ocean().swell() * _engine.effective(3);
    return std::max({ glow, _engine.thunder().flash(), _blink * .7f });
}
