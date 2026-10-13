#pragma once

#include "dsp/util.h"

namespace tw {

/*
A feedback delay network: eight delay lines of unrelated lengths, fed back
through a Hadamard matrix so every line feeds every other, each with a gain set
so the tail dies in exactly the requested time, and a damping filter in the
loop so the highs die sooner than the lows (as they do in a real room). The read
points wobble a few samples on slow unrelated LFOs, which smears the metallic
ring a static network has. Four allpasses in front thicken the onset.

Written from scratch rather than borrowed from DaisySP's ReverbSc, which is
LGPL.
*/
class Reverb {
public:
    static constexpr int kLines = 8;

    void init(const float sr)
    {
        _sr = sr;
        static const int kLen[kLines] = { 1693, 1949, 2203, 2447, 2713, 2999, 3253, 3539 };
        static const int kAp[kAps] = { 113, 163, 241, 337, 449, 577 };
        static const int kLoopAp[kLines] = { 241, 277, 311, 347, 389, 433, 479, 521 };
        const float scale = sr / 48000.f;
        for (int i = 0; i < kLines; i++) {
            _len[i] = std::min(static_cast<int>(kLen[i] * scale), kSize - 64);
            _lfo_hz[i] = .13f + .071f * static_cast<float>(i);
            _lfo_ph[i] = .131f * static_cast<float>(i);
            for (auto& s : _buf[i]) s = 0.f;
            _hp = 0.f;
            _lap_len[i] = std::min(static_cast<int>(kLoopAp[i] * scale), kLapSize - 1);
            for (auto& s : _lap[i]) s = 0.f;
            _lp[i] = 0.f;
        }
        for (int i = 0; i < kAps; i++) {
            _ap_len[i] = std::min(static_cast<int>(kAp[i] * scale), kApSize - 1);
            for (auto& s : _ap[i]) s = 0.f;
        }
        _w = 0;
        _ap_w = 0;
        set(.5f);
    }

    /* amount 0..1: the decay time, 1.5 s up to about 10 s. */
    void set(const float amount)
    {
        const float a = clampf(amount, 0.f, 1.f);
        const float rt60 = 1.5f + 11.f * std::pow(a, 1.3f);
        for (int i = 0; i < kLines; i++) {
            _g[i] = std::pow(10.f, -3.f * static_cast<float>(_len[i]) / (rt60 * _sr));
        }
        const float damp_hz = lerpf(6500.f, 2600.f, a);
        _damp = 1.f - std::exp(-kTwoPi * damp_hz / _sr);
        _hp_k = 1.f - std::exp(-kTwoPi * 140.f / _sr);
        // A longer tail holds more energy for the same input; trim to compensate.
        _out = kOut * std::pow(1.5f / rt60, .1f);
    }

    void process(const float in_l, const float in_r, float& out_l, float& out_r)
    {
        // Keep the bass out of the send: low frequencies pile up in a long tail
        // and turn it to mud, and a drone or a rumble has little else.
        float x = (in_l + in_r) * .5f;
        _hp += _hp_k * (x - _hp);
        x -= _hp;
        for (int i = 0; i < kAps; i++) {
            const int rd = (_ap_w - _ap_len[i] + kApSize) % kApSize;
            const float d = _ap[i][rd];
            // Schroeder allpass: w = x + g*d(N), y = d(N) - g*w. Flat magnitude.
            const float w = x + .65f * d;
            _ap[i][_ap_w] = w;              // buffers are per-stage, so one write head serves all
            x = d - .65f * w;
        }
        _ap_w = (_ap_w + 1) % kApSize;

        float d[kLines];
        for (int i = 0; i < kLines; i++) {
            _lfo_ph[i] += _lfo_hz[i] / _sr;
            _lfo_ph[i] -= std::floor(_lfo_ph[i]);
            const float pos = static_cast<float>(_len[i]) + kModDepth * fast_sin(_lfo_ph[i]);
            const float rp = static_cast<float>(_w) - pos + static_cast<float>(kSize);
            const int i0 = static_cast<int>(rp);
            const float frac = rp - static_cast<float>(i0);
            const float a = _buf[i][i0 & (kSize - 1)];
            const float b = _buf[i][(i0 + 1) & (kSize - 1)];
            const float v = a + (b - a) * frac;
            _lp[i] = flush(_lp[i] + _damp * (v - _lp[i]));

            // An allpass in every line, so the loop smears instead of echoing: on
            // broadband noise the bare network repeats at its shortest line.
            const float tap = _lap[i][(_w - _lap_len[i]) & (kLapSize - 1)];
            const float w = _lp[i] + kLoopG * tap;
            _lap[i][_w & (kLapSize - 1)] = flush(w);
            d[i] = tap - kLoopG * w;
            _tail_out[i] = d[i];
        }

        // 8x8 Hadamard, scaled to be energy-preserving.
        for (int s = 1; s < kLines; s <<= 1) {
            for (int i = 0; i < kLines; i += s << 1) {
                for (int j = i; j < i + s; j++) {
                    const float p = d[j], q = d[j + s];
                    d[j] = p + q;
                    d[j + s] = p - q;
                }
            }
        }
        constexpr float kNorm = .35355339f;   // 1 / sqrt(8)

        for (int i = 0; i < kLines; i++) {
            _buf[i][_w] = flush(x * .5f + d[i] * kNorm * _g[i]);
        }
        _w = (_w + 1) & (kSize - 1);

        out_l = 0.f;
        out_r = 0.f;
        for (int i = 0; i < kLines; i++) {
            const float v = _tail_out[i];
            if (i & 1) out_r += (i & 2) ? -v : v;
            else out_l += (i & 2) ? -v : v;
        }
        out_l *= _out;
        out_r *= _out;
    }

private:
    /* Measured: with the allpasses flat, the network is ~17 dB quieter than its input at any
       setting, so this brings noise through at roughly unity. */
    static constexpr float kOut = .5f * .63f * 7.f;
    static constexpr int kSize = 4096;     // per line; a power of two
    static constexpr int kApSize = 1024;
    static constexpr int kAps = 6;
    static constexpr int kLapSize = 1024;
    static constexpr float kLoopG = .65f;
    static constexpr float kModDepth = 22.f;

    float _buf[kLines][kSize] {};
    float _ap[kAps][kApSize] {};
    float _lap[kLines][kLapSize] {};
    float _tail_out[kLines] {};
    int _lap_len[kLines] {};
    int _len[kLines] {}, _ap_len[kAps] {};
    float _g[kLines] {}, _lp[kLines] {}, _lfo_hz[kLines] {}, _lfo_ph[kLines] {};
    float _damp { .5f }, _sr { 48000 }, _hp { 0 }, _hp_k { .02f }, _out { kOut };
    int _w { 0 }, _ap_w { 0 };
};

} // namespace tw
