#pragma once

#include "dsp/util.h"

namespace tw {

/*
Rain is two things that meet in the middle. Individual drops - a short sine
blip that chirps upward, like a drop landing in water - dominate a drizzle.
Their rate climbs exponentially with the knob, from a couple a second to a few
hundred, and at that point they fuse into the second thing: a bed of
band-limited hiss, which is what a downpour actually sounds like.
*/
class Rain {
public:
    void init(const float sr, const float control_rate, const uint32_t seed)
    {
        _sr = sr;
        _rng.seed(seed);
        _shimmer.init(seed + 5, 4.f, control_rate);
        for (int c = 0; c < 2; c++) _hiss_rng[c].seed(seed + 17 + 29 * c);
    }

    /* How many drops have been started since init (for tests). */
    uint32_t drops() const { return _drops; }
    static float drop_rate(const float level) { return 2.5f * std::exp(std::log(160.f) * level); }

    void process(const float level, float* l, float* r, const size_t n)
    {
        const float lvl = clampf(level, 0.f, 1.f);
        const bool idle = lvl <= 0.f;
        if (idle && _last_hiss <= 1e-5f && !_any_active()) return;

        const float shimmer = 1.f + .12f * _shimmer.tick();
        const float hiss = idle ? 0.f : std::pow(lvl, 1.6f) * kHissGain * shimmer;
        const float drop_amp = idle ? 0.f : std::pow(lvl, .5f) * kDropGain;
        const float p_drop = idle ? 0.f : drop_rate(lvl) / _sr;

        // The bed is steadier and brighter as the rain gets heavier.
        const float lo = 1500.f + 600.f * lvl;
        const float hi = 7500.f + 2500.f * lvl;
        for (int c = 0; c < 2; c++) {
            _hp[c].set(lo, .7f, _sr);
            _lp[c].set(hi, .7f, _sr);
        }

        float* out[2] = { l, r };
        const float h0 = _last_hiss;
        for (size_t i = 0; i < n; i++) {
            const float h = h0 + (hiss - h0) * (static_cast<float>(i) + 1.f) / static_cast<float>(n);
            for (int c = 0; c < 2; c++) {
                _hp[c].tick(_hiss_rng[c].bipolar());
                _lp[c].tick(_hp[c].hp);
                out[c][i] += _lp[c].lp * h;
            }

            if (_rng.uni() < p_drop) _spawn();

            for (auto& d : _drop) {
                if (!d.on) continue;
                d.phase += d.hz * (1.f + 1.4f * d.chirp) / _sr;
                d.phase -= std::floor(d.phase);
                const float y = fast_sin(d.phase) * d.env * d.amp * drop_amp;
                d.env *= d.decay;
                d.chirp *= d.chirp_decay;
                if (d.env < 1e-3f) d.on = false;
                l[i] += y * d.gl;
                r[i] += y * d.gr;
            }
        }
        _last_hiss = hiss;
    }

private:
    static constexpr float kHissGain = .30f;
    static constexpr float kDropGain = .20f;

    struct Drop {
        bool on { false };
        float phase { 0 }, hz { 0 }, env { 0 }, decay { 0 }, chirp { 0 }, chirp_decay { 0 }, amp { 0 }, gl { 0 }, gr { 0 };
    };

    bool _any_active() const { for (auto& d : _drop) if (d.on) return true; return false; }

    void _spawn()
    {
        _drops ++;
        Drop* slot = nullptr;
        float quietest = 1e9f;
        for (auto& d : _drop) {
            if (!d.on) { slot = &d; break; }
            if (d.env < quietest) { quietest = d.env; slot = &d; }
        }
        auto& d = *slot;
        d.on = true;
        d.phase = 0.f;
        d.hz = 1100.f * std::exp(std::log(5.f) * _rng.uni());           // 1.1k .. 5.5k, log-spread
        d.env = 1.f;
        const float tau = _rng.range(.003f, .012f) * (2200.f / d.hz) ;  // lower drops ring longer
        d.decay = std::exp(-1.f / (tau * _sr));
        d.chirp = 1.f;
        d.chirp_decay = std::exp(-1.f / (.012f * _sr));
        d.amp = .15f + .85f * _rng.uni() * _rng.uni() ;                  // mostly small, a few loud
        const float pan = _rng.uni();
        d.gl = std::cos(pan * kPi * .5f);
        d.gr = std::sin(pan * kPi * .5f);
    }

    Rng _rng { 1 };
    Rng _hiss_rng[2];
    Svf _hp[2], _lp[2];
    Wander _shimmer;
    Drop _drop[24];
    float _sr { 48000 };
    float _last_hiss { 0 };
    uint32_t _drops { 0 };
};

} // namespace tw
