#pragma once

/* Shared helpers for the host tests: rendering, measuring, writing WAVs. */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "dsp/engine.h"

namespace th {

constexpr float kSr = 48000.f;
constexpr size_t kBlock = 48;

struct Audio { std::vector<float> l, r; };

inline int g_failures = 0;
inline int g_checks = 0;

inline void check(const bool ok, const char* what)
{
    g_checks ++;
    if (!ok) { g_failures ++; std::printf("  FAIL  %s\n", what); }
}
#define CHECK(cond) th::check((cond), #cond)
#define CHECKF(cond, ...) do { char _b[256]; std::snprintf(_b, sizeof _b, __VA_ARGS__); th::check((cond), _b); } while (0)

inline void append(Audio& a, const float* l, const float* r, size_t n)
{
    a.l.insert(a.l.end(), l, l + n);
    a.r.insert(a.r.end(), r, r + n);
}

/* Run the engine for `seconds`, optionally calling `each(t)` before every block. */
template <class F>
Audio render(tw::Engine& e, const float seconds, F each)
{
    Audio a;
    float l[kBlock], r[kBlock];
    const size_t blocks = static_cast<size_t>(seconds * kSr / kBlock);
    for (size_t b = 0; b < blocks; b++) {
        each(static_cast<float>(b * kBlock) / kSr);
        e.process(l, r, kBlock);
        append(a, l, r, kBlock);
    }
    return a;
}
inline Audio render(tw::Engine& e, const float seconds) { return render(e, seconds, [](float) {}); }

inline std::vector<float> slice(const std::vector<float>& v, const float from_s, const float to_s)
{
    const size_t a = std::min(static_cast<size_t>(from_s * kSr), v.size());
    const size_t b = std::min(static_cast<size_t>(to_s * kSr), v.size());
    return std::vector<float>(v.begin() + a, v.begin() + b);
}

inline double rms(const std::vector<float>& v)
{
    if (v.empty()) return 0;
    double s = 0;
    for (float x : v) s += double(x) * x;
    return std::sqrt(s / v.size());
}
inline double db(const double x) { return 20.0 * std::log10(x + 1e-12); }
inline double peak(const std::vector<float>& v) { double p = 0; for (float x : v) p = std::fmax(p, std::fabs(x)); return p; }
inline bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }

inline std::vector<float> mono(const Audio& a)
{
    std::vector<float> m(a.l.size());
    for (size_t i = 0; i < m.size(); i++) m[i] = .5f * (a.l[i] + a.r[i]);
    return m;
}

/* Magnitude spectrum of the first N samples, Hann windowed. bin i = i * sr / N. */
inline std::vector<double> spectrum(const std::vector<float>& x, const size_t N)
{
    std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < N; i++) {
        const double w = .5 - .5 * std::cos(2 * M_PI * i / (N - 1));
        a[i] = i < x.size() ? x[i] * w : 0.0;
    }
    for (size_t i = 1, j = 0; i < N; i++) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= N; len <<= 1) {
        const double ang = -2 * M_PI / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < N; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; j++) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    std::vector<double> m(N / 2);
    for (size_t i = 0; i < N / 2; i++) m[i] = std::abs(a[i]);
    return m;
}

/* Power-average the magnitude spectrum over consecutive frames. */
inline std::vector<double> avg_spectrum(const std::vector<float>& x, const size_t N)
{
    std::vector<double> acc(N / 2, 0.0);
    size_t frames = 0;
    for (size_t at = 0; at + N <= x.size(); at += N, frames++) {
        auto s = spectrum(std::vector<float>(x.begin() + at, x.begin() + at + N), N);
        for (size_t i = 0; i < N / 2; i++) acc[i] += s[i] * s[i];
    }
    for (auto& v : acc) v = std::sqrt(v / std::max<size_t>(frames, 1));
    return acc;
}

inline double centroid(const std::vector<double>& s, const size_t N)
{
    double num = 0, den = 0;
    for (size_t i = 1; i < s.size(); i++) { num += s[i] * s[i] * i * kSr / N; den += s[i] * s[i]; }
    return den > 0 ? num / den : 0;
}

/* Fraction of spectral energy below `hz`. */
inline double energy_below(const std::vector<double>& s, const size_t N, const double hz)
{
    double lo = 0, all = 0;
    for (size_t i = 1; i < s.size(); i++) {
        const double e = s[i] * s[i];
        all += e;
        if (i * kSr / N < hz) lo += e;
    }
    return all > 0 ? lo / all : 0;
}

/* Frequency of the strongest bin in [lo, hi], parabolically interpolated. */
inline double peak_hz(const std::vector<double>& s, const size_t N, const double lo, const double hi)
{
    size_t best = 1;
    double bv = -1;
    for (size_t i = 1; i + 1 < s.size(); i++) {
        const double f = i * kSr / N;
        if (f < lo || f > hi) continue;
        if (s[i] > bv) { bv = s[i]; best = i; }
    }
    const double a = std::log(s[best - 1] + 1e-12), b = std::log(s[best] + 1e-12), c = std::log(s[best + 1] + 1e-12);
    const double d = .5 * (a - c) / (a - 2 * b + c);
    return (best + d) * kSr / N;
}

/* The loudest bin within +-2 of hz. */
inline double band_level(const std::vector<double>& s, const size_t N, const double hz)
{
    const size_t i = static_cast<size_t>(hz * N / kSr + .5);
    double m = 0;
    for (size_t k = (i > 2 ? i - 2 : 0); k <= i + 2 && k < s.size(); k++) m = std::fmax(m, s[k]);
    return m;
}

/* RMS in consecutive windows. */
inline std::vector<double> envelope(const std::vector<float>& x, const float win_s)
{
    const size_t w = static_cast<size_t>(win_s * kSr);
    std::vector<double> e;
    for (size_t at = 0; at + w <= x.size(); at += w) {
        double s = 0;
        for (size_t i = 0; i < w; i++) s += double(x[at + i]) * x[at + i];
        e.push_back(std::sqrt(s / w));
    }
    return e;
}

inline double correlation(const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0, aa = 0, bb = 0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) { ab += double(a[i]) * b[i]; aa += double(a[i]) * a[i]; bb += double(b[i]) * b[i]; }
    return ab / (std::sqrt(aa * bb) + 1e-18);
}

inline void write_wav(const std::string& path, const Audio& a)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t frames = static_cast<uint32_t>(a.l.size());
    const uint32_t data = frames * 4, rate = 48000, byte_rate = rate * 4;
    const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    const uint32_t riff = 36 + data, fmt_len = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt_len, 4, 1, f); std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < frames; i++) {
        for (float s : { a.l[i], a.r[i] }) {
            const int16_t v = static_cast<int16_t>(std::fmax(-1.f, std::fmin(1.f, s)) * 32767.f);
            std::fwrite(&v, 2, 1, f);
        }
    }
    std::fclose(f);
}

/* Master up, one field set, everything else off. */
inline tw::Params only(float tw::Params::* field, const float v)
{
    tw::Params p;
    p.*field = v;
    p.volume = .8f;
    return p;
}

} // namespace th
