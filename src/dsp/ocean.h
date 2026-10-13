#pragma once

#include "dsp/util.h"

namespace tw {

/*
Ocean: two layers of surf on unrelated clocks (9 and 13 seconds or so, each
re-rolled a little every wave), plus a constant deep bed.

One wave is a swell that accelerates upward, a crash at 55% of the cycle, and
an exponential wash back out. While it builds, a lowpass opens on noise, so the
sea brightens as it breaks; after the crash a thin band of foam fizz rides the
tail. The right ear hears each wave slightly later than the left.
*/
class Ocean {
public:
    void init(const float sr, const float control_rate, const uint32_t seed)
    {
        _sr = sr;
        _dt = 1.f / control_rate;
        Rng r(seed);
        for (int w = 0; w < 2; w++) {
            _wave[w].base = w == 0 ? 9.1f : 13.3f;
            _wave[w].period = _wave[w].base;
            _wave[w].phase = r.uni();
            _wave[w].rng.seed(seed + 3 + w);
        }
        for (int c = 0; c < 2; c++) {
            _rng[c].seed(seed + 71 + 13 * c);
            _fizz_rng[c].seed(seed + 191 + 17 * c);
            _bed_rng[c].seed(seed + 311 + 19 * c);
        }
    }

    /* 0..1: how far the first wave is through its cycle (for the LED). */
    float swell() const { return _swell; }

    static float shape(const float ph, float* fizz = nullptr)
    {
        constexpr float kCrash = .55f;
        float e, f = 0.f;
        if (ph < kCrash) {
            const float u = ph / kCrash;
            e = u * u * (3.f - 2.f * u);
            e *= e;                                   // accelerates into the crash
        }
        else {
            e = std::exp(-6.7f * (ph - kCrash));
            f = e;
        }
        if (fizz) *fizz = f;
        return e;
    }

    void process(const float level, float* l, float* r, const size_t n)
    {
        const float lvl = clampf(level, 0.f, 1.f);

        // Waves keep their time even when the knob is down, so turning it up
        // joins a sea that was already moving.
        const float speed = lerpf(1.f / 1.3f, 1.f / .8f, lvl);
        for (auto& w : _wave) {
            w.phase += _dt * speed / w.period;
            if (w.phase >= 1.f) {
                w.phase -= 1.f;
                w.period = w.base * w.rng.range(.85f, 1.15f);
            }
        }
        {
            float f;
            _swell = shape(_wave[0].phase, &f);
        }

        if (lvl <= 0.f && _last_gain <= 1e-5f) return;

        const float amp = std::pow(lvl, 1.3f) * kGain;
        float* out[2] = { l, r };
        constexpr float kOffset[2] = { 0.f, .07f };   // the right ear hears it later

        float fizz_amt[2] = { 0, 0 };
        for (int c = 0; c < 2; c++) {
            for (int w = 0; w < 2; w++) {
                float ph = _wave[w].phase - kOffset[c] * (w == 0 ? 1.f : .6f);
                if (ph < 0.f) ph += 1.f;
                float fz;
                const float e = shape(ph, &fz);
                const float bright = .6f + .4f * lvl;
                _surf[c][w].set(350.f + 4800.f * std::pow(e, 1.4f) * bright, .7f, _sr);
                _surf_amp[c][w] = .10f + .90f * e;
                fizz_amt[c] += fz * (w == 0 ? .6f : .4f);
            }
            _fizz[c].set(3400.f, .8f, _sr);
            _bed[c].set(160.f, .7f, _sr);
        }

        for (size_t i = 0; i < n; i++) {
            const float ramp = (static_cast<float>(i) + 1.f) / static_cast<float>(n);
            const float gain = _last_gain + (amp - _last_gain) * ramp;
            for (int c = 0; c < 2; c++) {
                float y = 0.f;
                for (int w = 0; w < 2; w++) {
                    _surf[c][w].tick(.35f * _pink[c][w].tick(_rng[c]) + .65f * _rng[c].bipolar());
                    y += _surf[c][w].lp * _surf_amp[c][w] * (w == 0 ? .75f : .6f);
                }
                _fizz[c].tick(_fizz_rng[c].bipolar());
                y += _fizz[c].hp * fizz_amt[c] * .10f;
                _bed[c].tick(_bed_rng[c].bipolar());
                y += _bed[c].lp * .2f;
                out[c][i] += y * gain;
            }
        }
        _last_gain = amp;
    }

private:
    static constexpr float kGain = 1.4f;

    struct Wave { float base { 10 }, period { 10 }, phase { 0 }; Rng rng; };
    Wave _wave[2];
    Rng _rng[2], _fizz_rng[2], _bed_rng[2];
    Svf _surf[2][2], _fizz[2], _bed[2];
    PinkNoise _pink[2][2];
    float _surf_amp[2][2] {};
    float _sr { 48000 }, _dt { .001f };
    float _last_gain { 0 };
    float _swell { 0 };
};

} // namespace tw
