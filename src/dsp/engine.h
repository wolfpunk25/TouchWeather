#pragma once

#include <algorithm>
#include "dsp/util.h"
#include "dsp/wind.h"
#include "dsp/rain.h"
#include "dsp/thunder.h"
#include "dsp/ocean.h"
#include "dsp/drone.h"
#include "dsp/reverb.h"

namespace tw {

/* What the panel asks for, every one 0..1. */
struct Params {
    float wind { 0 }, rain { 0 }, thunder { 0 }, ocean { 0 };
    float drone { 0 };     // level, and with it brightness
    float motion { 0 };    // how far the weather is allowed to wander by itself
    float reverb { 0 };
    float volume { 0 };
};

/*
The whole instrument: four weather generators and the drone summed onto one
bus, a reverb send, and a master with a soft clip at the end. Fixed block size.
*/
class Engine {
public:
    static constexpr size_t kMaxBlock = 128;

    void init(const float sr, const size_t block)
    {
        _sr = sr;
        _block = std::min(block, kMaxBlock);
        const float cr = sr / static_cast<float>(_block);
        _wind.init(sr, cr, 1001);
        _rain.init(sr, cr, 2002);
        _thunder.init(sr, cr, 3003);
        _ocean.init(sr, cr, 4004);
        _drone.init(sr, cr, 5005);
        _reverb.init(sr);
        _k_param = coef_tc(.04f, cr);
        _k_att = coef_tc(.03f, sr);
        _k_rel = coef_tc(1.2f, sr);
        static const float kPeriod[4] = { 29.f, 41.f, 53.f, 67.f };
        for (int i = 0; i < 4; i++) _walk[i].init(7000 + i, kPeriod[i], cr);
    }

    void set(const Params& p) { _target = p; }

    Drone& drone() { return _drone; }
    const Thunder& thunder() const { return _thunder; }
    const Ocean& ocean() const { return _ocean; }
    const Rain& rain() const { return _rain; }

    /* The levels the generators are really running at after motion (for tests). */
    float effective(const int i) const { return _eff[i]; }

    void process(float* out_l, float* out_r, const size_t n)
    {
        // Smooth the panel, then let the weather wander around it.
        _cur.wind += (_target.wind - _cur.wind) * _k_param;
        _cur.rain += (_target.rain - _cur.rain) * _k_param;
        _cur.thunder += (_target.thunder - _cur.thunder) * _k_param;
        _cur.ocean += (_target.ocean - _cur.ocean) * _k_param;
        _cur.drone += (_target.drone - _cur.drone) * _k_param;
        _cur.motion += (_target.motion - _cur.motion) * _k_param;
        _cur.reverb += (_target.reverb - _cur.reverb) * _k_param;
        _cur.volume += (_target.volume - _cur.volume) * _k_param;

        const float base[4] = { _cur.wind, _cur.rain, _cur.thunder, _cur.ocean };
        for (int i = 0; i < 4; i++) {
            const float w = _walk[i].tick();
            _eff[i] = clampf(base[i] * (1.f + _cur.motion * .65f * w), 0.f, 1.f);
        }

        float* dl = _dry_l;
        float* dr = _dry_r;
        std::fill(dl, dl + n, 0.f);
        std::fill(dr, dr + n, 0.f);
        _wind.process(_eff[0], dl, dr, n);
        _rain.process(_eff[1], dl, dr, n);
        _thunder.process(_eff[2], dl, dr, n);
        _ocean.process(_eff[3], dl, dr, n);
        _drone.process(_cur.drone, dl, dr, n);
        for (size_t i = 0; i < n; i++) { dl[i] *= kBus; dr[i] *= kBus; }

        // Reverb: the amount sets the tail length as well as how much is heard.
        const float rv = clampf(_cur.reverb, 0.f, 1.f);
        _reverb.set(rv);
        const float wet = rv <= 0.f ? 0.f : std::pow(rv, .8f) * kWet;
        const float dry = 1.f - .45f * rv;

        for (size_t i = 0; i < n; i++) {
            float wl = 0.f, wr = 0.f;
            if (rv > 0.f || _tail > 0) _reverb.process(dl[i], dr[i], wl, wr);
            dl[i] = dl[i] * dry + wl * wet;
            dr[i] = dr[i] * dry + wr * wet;
            const float a = std::max(std::fabs(dl[i]), std::fabs(dr[i]));
            _env += (a > _env ? _k_att : _k_rel) * (a - _env);
        }

        // A slow leveller keeps a full house of weather from simply getting
        // louder: each element alone is untouched, only the pile-up is eased.
        float lev = 1.f;
        if (_env > kThreshold) lev = std::pow(_env / kThreshold, 1.f / kRatio - 1.f);
        const float lev0 = _last_lev;

        const float vol = clampf(_cur.volume, 0.f, 1.f);
        const float g1 = vol * vol;
        const float g0 = _last_gain;
        for (size_t i = 0; i < n; i++) {
            const float t = (static_cast<float>(i) + 1.f) / static_cast<float>(n);
            const float g = (g0 + (g1 - g0) * t) * (lev0 + (lev - lev0) * t);
            out_l[i] = soft_clip(dl[i] * g);
            out_r[i] = soft_clip(dr[i] * g);
        }
        _last_gain = g1;
        _last_lev = lev;
        // Keep the network running for a while after the send is closed so a
        // tail is not cut mid-ring.
        _tail = rv > 0.f ? static_cast<int>(_sr * 14.f / static_cast<float>(n)) : std::max(0, _tail - 1);
    }

private:
    static constexpr float kBus = 2.0f;        // generators are trimmed to mix, not to be heard alone
    static constexpr float kWet = 2.0f;
    static constexpr float kThreshold = .22f;   // about -13 dBFS
    static constexpr float kRatio = 3.f;

    Wind _wind;
    Rain _rain;
    Thunder _thunder;
    Ocean _ocean;
    Drone _drone;
    Reverb _reverb;
    Wander _walk[4];
    Params _target, _cur;
    float _eff[4] {};
    float _dry_l[kMaxBlock] {}, _dry_r[kMaxBlock] {};
    float _sr { 48000 }, _k_param { .02f };
    float _last_gain { 0 }, _last_lev { 1 }, _env { 0 }, _k_att { .001f }, _k_rel { .00003f };
    size_t _block { 48 };
    int _tail { 0 };
};

} // namespace tw
