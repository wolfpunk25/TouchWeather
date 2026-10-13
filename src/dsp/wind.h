#pragma once

#include "dsp/util.h"

namespace tw {

/*
Wind: pink noise through a wide moving band (the body of the gust) and a narrow
one (the whistle), with the centre frequencies and the gust level each wandering
on their own slow clocks. Left and right are separate noise and separate
wanderers, so the wind drifts around the head rather than sitting in the middle.
More wind means louder, higher, and gustier.
*/
class Wind {
public:
    void init(const float sr, const float control_rate, const uint32_t seed)
    {
        _sr = sr;
        for (int c = 0; c < 2; c++) {
            auto& s = _ch[c];
            s.rng.seed(seed + 101 * c);
            s.gust.init(seed + 11 + c, 7.f + 2.f * c, control_rate);
            s.body_cf.init(seed + 23 + c, 11.f + 3.f * c, control_rate);
            s.whistle_cf.init(seed + 37 + c, 5.f + c, control_rate);
            s.flutter.init(seed + 41 + c, .8f, control_rate);
        }
    }

    void process(const float level, float* l, float* r, const size_t n)
    {
        if (level <= 0.f && _last_gain[0] <= 1e-5f && _last_gain[1] <= 1e-5f) return;

        const float lvl = clampf(level, 0.f, 1.f);
        const float amp = std::pow(lvl, 1.5f) * kGain;
        float* out[2] = { l, r };

        for (int c = 0; c < 2; c++) {
            auto& s = _ch[c];

            // Stronger wind gusts harder: the floor falls and the swing grows.
            const float g = .5f + .5f * s.gust.tick();
            const float depth = .35f + .5f * lvl;
            float gust = 1.f - depth * (1.f - g);
            gust *= 1.f + .12f * s.flutter.tick();
            gust = clampf(gust, 0.f, 1.5f);

            const float body_hz = (170.f + 520.f * lvl) * std::exp2(.7f * s.body_cf.tick());
            const float whistle_hz = (900.f + 1700.f * lvl) * std::exp2(.8f * s.whistle_cf.tick());
            s.body.set(body_hz, 1.1f, _sr);
            s.whistle.set(whistle_hz, 7.f, _sr);

            const float g0 = _last_gain[c];
            const float g1 = amp * gust * gust;
            const float whistle_amt = .25f * lvl * g;   // only when it is blowing
            for (size_t i = 0; i < n; i++) {
                const float gain = g0 + (g1 - g0) * (static_cast<float>(i) + 1.f) / static_cast<float>(n);
                const float x = s.pink.tick(s.rng);
                s.body.tick(x);
                s.whistle.tick(x);
                out[c][i] += (s.body.bpn * 1.4f + s.whistle.bpn * whistle_amt * 2.2f) * gain;
            }
            _last_gain[c] = g1;
        }
    }

private:
    static constexpr float kGain = .9f;

    struct Channel {
        Rng rng;
        PinkNoise pink;
        Svf body, whistle;
        Wander gust, body_cf, whistle_cf, flutter;
    };
    Channel _ch[2];
    float _sr { 48000 };
    float _last_gain[2] { 0, 0 };
};

} // namespace tw
