#pragma once

#include <cstdint>
#include "dsp/util.h"

namespace tw {

/*
What the pads play. Five-note scales only: with no semitones and no tritones
in them, any handful of pads sounds like a chord rather than a clash, which is
what lets the instrument be played by touch and also left alone.
*/
namespace scales {
constexpr int kCount = 3;
constexpr int kNotes = 5;
constexpr int kStep[kCount][kNotes] = {
    { 0, 2, 4, 7, 9 },     // major pentatonic: bright, open
    { 0, 3, 5, 7, 10 },    // minor pentatonic: dark, folk
    { 0, 2, 5, 7, 10 },    // suspended pentatonic: neither major nor minor
};
} // namespace scales

struct Tuning {
    int root { 2 };        // pitch class, 0 = C ... 11 = B. D by default.
    int scale { 1 };       // index into scales::kStep
    int octave { 0 };      // register shift, in octaves: -1, 0, +1
};

/* Pad n (0..9) is the nth step of the scale, running up two octaves from the root. */
inline float degree_midi(const Tuning& t, const int degree)
{
    const int step = scales::kStep[t.scale][degree % scales::kNotes];
    return static_cast<float>(36 + t.root + 12 * t.octave + step + 12 * (degree / scales::kNotes));
}

inline float midi_hz(const float m) { return 440.f * std::exp2((m - 69.f) / 12.f); }

/*
Eight voices of slow, wide pad. Each is two detuned polyBLEP saws, a sine at the
fundamental and a quiet sine an octave down, through a lowpass whose cutoff
follows the note. Everything drifts: detune, filter and pan each ride their own
very slow LFO, no two voices on the same phase, so a held chord keeps shifting
and never loops.

The caller says which degrees should sound (a bitmask) and what the tuning is.
The voices work out for themselves who comes in, who goes out, and who glides
to a new pitch when the root or scale changes.
*/
class Drone {
public:
    static constexpr int kVoices = 8;
    static constexpr int kDegrees = 10;

    void init(const float sr, const float control_rate, const uint32_t seed)
    {
        _sr = sr;
        _k_glide = coef_tc(1.4f, control_rate);
        _k_attack = coef_tc(1.1f, control_rate);
        _k_release = coef_tc(2.2f, control_rate);
        Rng r(seed);
        static const float kPan[kVoices] = { .5f, .32f, .68f, .15f, .85f, .42f, .58f, .25f };
        for (int i = 0; i < kVoices; i++) {
            auto& v = _v[i];
            for (int k = 0; k < 3; k++) v.lfo_ph[k] = r.uni();
            v.lfo_hz[0] = r.range(.05f, .11f);
            v.lfo_hz[1] = r.range(.03f, .08f);
            v.lfo_hz[2] = r.range(.02f, .05f);
            v.pan = kPan[i];
        }
    }

    /* mask bit n = pad n latched. Cheap; call whenever either changes. */
    void set(const uint16_t mask, const Tuning& t)
    {
        for (auto& v : _v) {
            if (!v.active()) continue;
            if (v.gate && !(mask & (1u << v.degree))) v.gate = false;
            v.target = midi_hz(degree_midi(t, v.degree));
        }
        for (int d = 0; d < kDegrees; d++) {
            if (!(mask & (1u << d))) continue;
            Voice* mine = nullptr;
            for (auto& v : _v) if (v.active() && v.degree == d) { mine = &v; break; }
            if (mine) {
                mine->gate = true;
                continue;
            }
            // A free voice, else the quietest releasing one, else the oldest.
            Voice* slot = nullptr;
            for (auto& v : _v) if (!v.active()) { slot = &v; break; }
            if (!slot) {
                float best = 1e9f;
                for (auto& v : _v) if (!v.gate && v.env < best) { best = v.env; slot = &v; }
            }
            if (!slot) {
                uint32_t oldest = UINT32_MAX;
                for (auto& v : _v) if (v.born < oldest) { oldest = v.born; slot = &v; }
            }
            slot->degree = d;
            slot->gate = true;
            slot->env = 0.f;
            slot->born = ++_serial;
            slot->target = slot->freq = midi_hz(degree_midi(t, d));
            for (auto& p : slot->ph) p = 0.f;
        }
    }

    int sounding() const { int n = 0; for (auto& v : _v) if (v.active()) n++; return n; }
    int gated() const { int n = 0; for (auto& v : _v) if (v.gate) n++; return n; }

    /* level 0..1 sets loudness, and with it brightness. */
    void process(const float level, float* l, float* r, const size_t n)
    {
        const float lvl = clampf(level, 0.f, 1.f);
        const float amp = lvl * lvl * kGain;
        const float dt = static_cast<float>(n) / _sr;

        // Many notes at once must not simply stack into a wall.
        const int live = sounding();
        const float want_norm = live > 1 ? 1.f / std::sqrt(static_cast<float>(live)) : 1.f;
        _norm += (want_norm - _norm) * _k_glide;

        for (auto& v : _v) {
            if (!v.active()) continue;

            v.freq += (v.target - v.freq) * _k_glide;
            v.env += ((v.gate ? 1.f : 0.f) - v.env) * (v.gate ? _k_attack : _k_release);
            if (!v.gate && v.env < 1e-3f) { v.env = 0.f; v.degree = -1; continue; }

            for (int k = 0; k < 3; k++) {
                v.lfo_ph[k] += v.lfo_hz[k] * dt;
                v.lfo_ph[k] -= std::floor(v.lfo_ph[k]);
            }
            const float la = fast_sin(v.lfo_ph[0]);
            const float lb = fast_sin(v.lfo_ph[1]);
            const float lc = fast_sin(v.lfo_ph[2]);

            const float f = v.freq;
            const float inc1 = f * (1.f + .0028f + .0014f * la) / _sr;
            const float inc2 = f * (1.f - .0031f - .0014f * lb) / _sr;
            const float inc3 = f / _sr;
            const float inc4 = f * .5f / _sr;
            v.filt.set(f * (1.6f + 5.f * lvl) * (1.f + .35f * lb), .85f, _sr);

            const float pan = clampf(v.pan + .2f * lc, 0.f, 1.f);
            const float gl = std::cos(pan * kPi * .5f);
            const float gr = std::sin(pan * kPi * .5f);
            const float g = v.env * amp * _norm;

            for (size_t i = 0; i < n; i++) {
                v.ph[0] += inc1; v.ph[0] -= std::floor(v.ph[0]);
                v.ph[1] += inc2; v.ph[1] -= std::floor(v.ph[1]);
                v.ph[2] += inc3; v.ph[2] -= std::floor(v.ph[2]);
                v.ph[3] += inc4; v.ph[3] -= std::floor(v.ph[3]);
                const float s1 = 2.f * v.ph[0] - 1.f - blep(v.ph[0], inc1);
                const float s2 = 2.f * v.ph[1] - 1.f - blep(v.ph[1], inc2);
                v.filt.tick(.5f * (s1 + s2));
                const float y = v.filt.lp * .8f + fast_sin(v.ph[2]) * .45f + fast_sin(v.ph[3]) * .3f;
                l[i] += y * g * gl;
                r[i] += y * g * gr;
            }
        }
    }

private:
    static constexpr float kGain = .30f;

    static float blep(float t, const float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }

    struct Voice {
        int degree { -1 };
        bool gate { false };
        float env { 0 }, freq { 0 }, target { 0 }, pan { .5f };
        float ph[4] {}, lfo_ph[3] {}, lfo_hz[3] {};
        uint32_t born { 0 };
        Svf filt;
        bool active() const { return degree >= 0; }
    };

    Voice _v[kVoices];
    float _sr { 48000 };
    float _k_glide { .001f }, _k_attack { .001f }, _k_release { .001f };
    float _norm { 1 };
    uint32_t _serial { 0 };
};

} // namespace tw
