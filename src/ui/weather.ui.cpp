#include "ui/weather.ui.h"

#include <algorithm>

using namespace tw;

void WeatherUI::init(const PanelInput& in)
{
    _apply_tuning(in);
    _user = (1u << 0) | (1u << 3);   // root and fifth: an open drone out of the box
    _prev_seen = in.pads_seen | in.pads;
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
    // Switch B: the register.
    switch (in.sw[1]) {
        case PanelInput::SwUp:   _tuning.octave = 1; break;
        case PanelInput::SwDown: _tuning.octave = -1; break;
        default:                 _tuning.octave = 0; break;
    }
}

void WeatherUI::_push()
{
    _engine.drone().set(_user | _ghost, _tuning);
}

void WeatherUI::_pad(const int role, const bool shift)
{
    _blink = .8f;
    if (role == layout::kRoleShift) return;

    if (shift) {
        _tuning.root = role;   // pads 0..10 -> C..A#
        _push();
        return;
    }
    if (role == layout::kRoleClear) {
        _user = 0;
        _ghost = 0;
    }
    else {
        const uint16_t bit = 1u << role;
        if (_ghost & bit) _ghost &= ~bit;   // tapping a ghost sends it away
        else _user ^= bit;
    }
    _push();
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

    bool dirty = _tuning.scale != before.scale || _tuning.octave != before.octave;

    if (in.pads_ok) {
        // A pad counts as touched if it was held at any point since the last poll.
        const uint16_t seen = in.pads_seen | in.pads;
        const uint16_t rising = seen & ~_prev_seen;
        _prev_seen = seen;

        const bool shift = seen & (1u << layout::kPad[layout::kRoleShift]);
        for (int role = 0; role < layout::kRoles; role++) {
            if (rising & (1u << layout::kPad[role])) _pad(role, shift);
        }
    }
    else {
        _prev_seen = 0;
    }

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
