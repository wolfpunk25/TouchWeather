#pragma once

#include <algorithm>
#include "dsp/util.h"

namespace tw {

/*
Thunder, in two parts.

A faint rumble bed (pink noise, low-passed) that is always there once the knob
is up: distant weather you can feel more than hear.

Strikes, on a random clock. Turning the knob up brings them closer in time and
closer in space. A strike has a "nearness": the sound arrives after a delay
(light is faster than sound), a near one starts with a crack, and the rumble is
a handful of overlapping swells at random offsets with the lowpass sliding down
as each dies away, which is what makes it roll rather than just boom.

flash() is 1.0 at the instant of lightning and decays; the LED shows it.
*/
class Thunder {
public:
    void init(const float sr, const float control_rate, const uint32_t seed)
    {
        _sr = sr;
        _dt = 1.f / control_rate;
        _rng.seed(seed);
        for (int c = 0; c < 2; c++) _noise_rng[c].seed(seed + 7 + 31 * c);
        _crack_rng.seed(seed + 99);
        _bed_wander.init(seed + 13, 17.f, control_rate);
        _countdown = 4.f;
    }

    float flash() const { return _flash; }
    uint32_t strikes() const { return _strikes; }

    /* nearness 0 = far away, 1 = overhead. */
    void strike(const float nearness)
    {
        _strikes ++;
        _flash = .35f + .65f * nearness;
        _pending = true;
        _pending_near = nearness;
        _pending_wait = .15f + (1.f - nearness) * 2.2f;
    }

    static float mean_interval(const float level) { return lerpf(110.f, 14.f, level); }

    void process(const float level, float* l, float* r, const size_t n)
    {
        const float lvl = clampf(level, 0.f, 1.f);
        const float block_s = static_cast<float>(n) / _sr;

        _flash *= std::exp(-block_s / .35f);
        if (_flash < 1e-3f) _flash = 0.f;

        // The clock only runs while the knob is up.
        if (lvl > .03f) {
            _countdown -= block_s;
            if (_countdown <= 0.f) {
                strike(clampf(std::pow(_rng.uni(), .8f) * (.45f + .6f * lvl), 0.f, 1.f));
                const float u = _rng.uni();
                _countdown = std::max(7.f, -mean_interval(lvl) * std::log(1.f - .999f * u));
            }
        }

        if (_pending) {
            _pending_wait -= block_s;
            if (_pending_wait <= 0.f) { _pending = false; _rumble(_pending_near); }
        }

        const float bed_wander = .5f + .5f * _bed_wander.tick();
        const float bed = lvl > 0.f ? std::pow(lvl, 1.2f) * (.35f + .65f * bed_wander) * kBed : 0.f;
        const float amp = lvl > 0.f ? (.4f + .6f * lvl) * kEvent : 0.f;

        // The total of the swells, and how far the rumble sits in the dark.
        float env = 0.f;
        for (auto& p : _pulse) {
            if (!p.on) continue;
            if (p.delay > 0.f) { p.delay -= block_s; continue; }
            if (p.age < p.attack) {
                p.level = p.peak * (p.age / p.attack);
                p.age += block_s;
            }
            else {
                p.level *= std::exp(-block_s / p.tau);
                if (p.level < 1e-3f) p.on = false;
            }
            env += p.level;
        }
        env = std::min(env, 1.5f);

        const float cutoff = 70.f + (110.f + 700.f * _event_near) * env;
        // Close thunder growls: some of the upper rumble comes through with it.
        const float mid_amt = .15f + .45f * _event_near;
        const float crack_target = _crack > 1e-3f ? _crack : 0.f;
        float* out[2] = { l, r };
        for (int c = 0; c < 2; c++) {
            _lp[c].set(cutoff, .7f, _sr);
            _mid[c].set(cutoff * 3.5f, .7f, _sr);
            _bed_lp[c].set(110.f, .7f, _sr);
            _crack_bp.set(1500.f + 1500.f * _event_near, .8f, _sr);
        }

        const float e0 = _last_env, e1 = env * amp;
        const float b0 = _last_bed;
        for (size_t i = 0; i < n; i++) {
            const float ramp = (static_cast<float>(i) + 1.f) / static_cast<float>(n);
            const float ge = e0 + (e1 - e0) * ramp;
            const float gb = b0 + (bed - b0) * ramp;
            for (int c = 0; c < 2; c++) {
                const float w = _pink[c].tick(_noise_rng[c]);
                _lp[c].tick(w);
                _mid[c].tick(w);
                _bed_lp[c].tick(w);
                out[c][i] += (_lp[c].lp + _mid[c].lp * mid_amt) * ge + _bed_lp[c].lp * gb;
            }
            if (crack_target > 0.f || _crack > 1e-3f) {
                _crack_bp.tick(_crack_rng.bipolar());
                const float cr = _crack_bp.bpn * _crack * amp * kCrack;
                _crack *= _crack_decay;
                l[i] += cr; r[i] += cr;
            }
        }
        _last_env = e1;
        _last_bed = bed;
    }

private:
    static constexpr float kEvent = .45f;
    static constexpr float kBed = .12f;
    static constexpr float kCrack = .35f;

    struct Pulse { bool on { false }; float delay { 0 }, age { 0 }, attack { .1f }, peak { 0 }, level { 0 }, tau { 1 }; };

    void _rumble(const float nearness)
    {
        _event_near = nearness;
        const int count = 3 + static_cast<int>(nearness * 3.f + _rng.uni() * 2.f);
        const float spread = 1.5f + 5.f * (1.f - nearness);
        for (int i = 0; i < count; i++) {
            Pulse* slot = nullptr;
            for (auto& p : _pulse) if (!p.on) { slot = &p; break; }
            if (!slot) break;
            slot->on = true;
            slot->age = 0.f;
            slot->level = 0.f;
            slot->delay = i == 0 ? 0.f : _rng.range(.3f, spread);
            slot->attack = i == 0 ? .04f + (1.f - nearness) * .5f : _rng.range(.15f, .7f);
            slot->peak = (i == 0 ? 1.f : _rng.range(.3f, .85f)) * (.45f + .55f * nearness);
            slot->tau = _rng.range(.9f, 2.4f) * (.8f + .6f * nearness);
        }
        if (nearness > .45f) _crack = nearness * nearness;
    }

    Rng _rng { 1 }, _noise_rng[2], _crack_rng;
    PinkNoise _pink[2];
    Svf _lp[2], _mid[2], _bed_lp[2], _crack_bp;
    Wander _bed_wander;
    Pulse _pulse[12];
    float _sr { 48000 }, _dt { .001f };
    float _countdown { 4 };
    float _flash { 0 };
    float _crack { 0 }, _crack_decay { .9993f };
    float _event_near { .5f };
    bool _pending { false };
    float _pending_near { 0 }, _pending_wait { 0 };
    float _last_env { 0 }, _last_bed { 0 };
    uint32_t _strikes { 0 };
};

} // namespace tw
