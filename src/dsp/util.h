#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tw {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf(const float x, const float lo, const float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(const float a, const float b, const float t) { return a + (b - a) * t; }
inline float flush(const float x) { return std::fabs(x) < 1e-18f ? 0.f : x; }

/* One-pole smoothing coefficient for a time constant, at a given call rate. */
inline float coef_tc(const float seconds, const float rate)
{
    return seconds <= 0.f ? 1.f : 1.f - std::exp(-1.f / (seconds * rate));
}

/* Parabolic sine from a 0..1 phase. Good enough for water drops. */
inline float fast_sin(const float x)
{
    return x < .5f ? 16.f * x * (.5f - x) : -16.f * (x - .5f) * (1.f - x);
}

/* xorshift32. Every generator owns one, so a seed fully fixes its behaviour. */
class Rng {
public:
    explicit Rng(const uint32_t seed = 1) : _s(seed ? seed : 1) {}
    void seed(const uint32_t s) { _s = s ? s : 1; }
    uint32_t next() { _s ^= _s << 13; _s ^= _s >> 17; _s ^= _s << 5; return _s; }
    float bipolar() { return static_cast<int32_t>(next()) * (1.f / 2147483648.f); }   // -1..1
    float uni() { return (next() >> 8) * (1.f / 16777216.f); }                          // 0..1
    float range(const float lo, const float hi) { return lo + (hi - lo) * uni(); }
private:
    uint32_t _s;
};

/* Cytomic's trapezoidal state-variable filter. bpn is the unity-peak bandpass. */
class Svf {
public:
    void set(const float hz, const float q, const float sr)
    {
        const float f = clampf(hz, 8.f, .45f * sr);
        const float g = std::tan(kPi * f / sr);
        _k = 1.f / q;
        _a1 = 1.f / (1.f + g * (g + _k));
        _a2 = g * _a1;
        _a3 = g * _a2;
    }
    void tick(const float v0)
    {
        const float v3 = v0 - _ic2;
        const float v1 = _a1 * _ic1 + _a2 * v3;
        const float v2 = _ic2 + _a2 * _ic1 + _a3 * v3;
        _ic1 = flush(2.f * v1 - _ic1);
        _ic2 = flush(2.f * v2 - _ic2);
        lp = v2;
        bp = v1;
        hp = v0 - _k * v1 - v2;
        bpn = _k * v1;
    }
    float lp { 0 }, bp { 0 }, hp { 0 }, bpn { 0 };
private:
    float _k { 1 }, _a1 { 0 }, _a2 { 0 }, _a3 { 0 }, _ic1 { 0 }, _ic2 { 0 };
};

/* Pink-ish noise (Paul Kellet's three pole approximation). */
class PinkNoise {
public:
    float tick(Rng& rng)
    {
        const float w = rng.bipolar();
        _b0 = .99886f * _b0 + w * .0555179f;
        _b1 = .99332f * _b1 + w * .0750759f;
        _b2 = .96900f * _b2 + w * .1538520f;
        return (_b0 + _b1 + _b2 + w * .1848f) * .5f;
    }
private:
    float _b0 { 0 }, _b1 { 0 }, _b2 { 0 };
};

/* Brown noise: a leaky integral of white. Rolls off at 6 dB/octave. */
class BrownNoise {
public:
    float tick(Rng& rng)
    {
        _z = flush((_z + .04f * rng.bipolar()) * .998f);
        return _z * 3.f;
    }
private:
    float _z { 0 };
};

/*
A slow random walk, -1..1: it picks a new target every `period` seconds or so
(give or take 40%) and glides to it. Call tick() at a steady control rate.
*/
class Wander {
public:
    void init(const uint32_t seed, const float period_s, const float control_rate)
    {
        _rng.seed(seed);
        _period = period_s;
        _rate = control_rate;
        _k = coef_tc(period_s * .35f, control_rate);
        _target = _rng.bipolar();
        _cur = _target;
        _left = period_s * control_rate;
    }
    float tick()
    {
        if (--_left <= 0.f) {
            _target = _rng.bipolar();
            _left = _period * _rate * _rng.range(.6f, 1.4f);
        }
        _cur += (_target - _cur) * _k;
        return _cur;
    }
private:
    Rng _rng { 1 };
    float _period { 10 }, _rate { 1000 }, _k { .001f };
    float _target { 0 }, _cur { 0 }, _left { 0 };
};

/* Transparent below +-0.7, then bends towards +-1 rather than clipping. */
inline float soft_clip(const float x)
{
    const float a = std::fabs(x);
    if (a < .7f) return x;
    const float y = .7f + .3f * std::tanh((a - .7f) * (1.f / .3f));
    return x < 0.f ? -y : y;
}

} // namespace tw
