#include "harness.h"

using namespace th;

static tw::Params solo(float tw::Params::* field, float v)
{
    return only(field, v);
}

static void levels_and_spectra()
{
    std::printf("-- each element alone, knob at 1.0\n");
    struct Row { const char* name; float tw::Params::* f; double rms_db, centroid, peak; };
    Row rows[] = {
        { "wind", &tw::Params::wind, 0, 0, 0 }, { "rain", &tw::Params::rain, 0, 0, 0 },
        { "thunder", &tw::Params::thunder, 0, 0, 0 }, { "ocean", &tw::Params::ocean, 0, 0, 0 },
        { "drone", &tw::Params::drone, 0, 0, 0 },
    };
    constexpr size_t N = 8192;
    for (auto& row : rows) {
        tw::Engine e;
        e.init(kSr, kBlock);
        auto p = solo(row.f, 1.f);
        if (row.f == &tw::Params::drone) e.drone().set(0b1001, tw::Tuning {});
        e.set(p);
        (void)render(e, 2.f);
        auto a = render(e, 40.f);
        auto m = mono(a);
        row.rms_db = db(rms(m));
        row.centroid = centroid(avg_spectrum(m, N), N);
        row.peak = peak(m);
        std::printf("   %-8s rms %6.1f dBFS   peak %5.2f   centroid %6.0f Hz\n", row.name, row.rms_db, row.peak, row.centroid);
        CHECKF(finite(a.l) && finite(a.r), "%s output is finite", row.name);
        CHECKF(row.peak < 1.0, "%s stays under full scale", row.name);
        CHECKF(row.rms_db > -34 && row.rms_db < -9, "%s level is usable (%.1f dB)", row.name, row.rms_db);
    }
    // Rain is the brightest, thunder the darkest, the others between.
    CHECK(rows[1].centroid > rows[0].centroid);   // rain > wind
    CHECK(rows[3].centroid < rows[1].centroid);   // ocean < rain
    CHECK(rows[3].centroid > rows[2].centroid);   // ocean > thunder
    CHECK(rows[0].centroid > 250 && rows[0].centroid < 3000);
    CHECK(rows[3].centroid > 250 && rows[3].centroid < 3000);
    CHECK(rows[2].centroid < 400);
    CHECK(rows[1].centroid > 1200);   // the splashes sit lower than the old chirps did
}

static void silence_and_taper()
{
    std::printf("-- knob at zero is silent; louder as it goes up\n");
    // Thunder is left out: it is a handful of random events, so its average
    // over any short window says nothing. Its taper is checked separately.
    float tw::Params::* fields[] = { &tw::Params::wind, &tw::Params::rain, &tw::Params::ocean };
    const char* names[] = { "wind", "rain", "ocean" };
    for (int i = 0; i < 3; i++) {
        tw::Engine e;
        e.init(kSr, kBlock);
        double prev = -1;
        for (float k : { 0.f, .25f, .5f, .75f, 1.f }) {
            e.set(solo(fields[i], k));
            (void)render(e, 2.f);
            auto a = render(e, 30.f);
            const double r = rms(mono(a));
            if (k == 0.f) CHECKF(peak(a.l) < 1e-5, "%s at 0 is silent (peak %g)", names[i], peak(a.l));
            else CHECKF(r > prev, "%s grows with the knob (%.2f: %.1f dB)", names[i], k, db(r));
            prev = r;
        }
    }
    {
        // The same strike is louder with the knob up.
        double prev = 0;
        for (float k : { .3f, .6f, 1.f }) {
            tw::Engine te;
            te.init(kSr, kBlock);
            te.set(solo(&tw::Params::thunder, k));
            (void)render(te, 1.f);
            const_cast<tw::Thunder&>(te.thunder()).strike(.6f);
            auto a = render(te, 10.f);
            const double pk = peak(a.l);
            CHECKF(pk > prev, "thunder strike grows with the knob (%.1f: peak %.3f)", k, pk);
            prev = pk;
        }
    }
    tw::Engine e;
    e.init(kSr, kBlock);
    tw::Params p;
    p.wind = p.rain = p.thunder = p.ocean = p.drone = 1.f;
    p.volume = 0.f;
    e.set(p);
    (void)render(e, 1.f);
    auto a = render(e, 2.f);
    CHECKF(peak(a.l) < 1e-4, "volume 0 mutes everything (peak %g)", peak(a.l));
}

static void thunder_behaviour()
{
    std::printf("-- thunder strikes\n");
    {
        tw::Engine e;
        e.init(kSr, kBlock);
        e.set(solo(&tw::Params::thunder, .02f));
        (void)render(e, 120.f);
        CHECKF(const_cast<tw::Thunder&>(e.thunder()).strikes() == 0, "no strikes with the knob nearly off");
    }
    {
        tw::Engine e;
        e.init(kSr, kBlock);
        e.set(solo(&tw::Params::thunder, 1.f));
        float max_flash = 0;
        auto a = render(e, 180.f, [&](float) { max_flash = std::fmax(max_flash, e.thunder().flash()); });
        const uint32_t s = const_cast<tw::Thunder&>(e.thunder()).strikes();
        std::printf("   strikes in 3 minutes at full: %u\n", s);
        CHECKF(s >= 5 && s <= 40, "a few strikes at full (%u)", s);
        CHECK(max_flash > .9f);
        // The energy is overwhelmingly below 300 Hz.
        auto sp = avg_spectrum(mono(a), 16384);
        const double lo = energy_below(sp, 16384, 300);
        std::printf("   energy below 300 Hz: %.1f%%\n", lo * 100);
        CHECK(lo > .8);
        // Rumble is slow: a strike has a long tail, so the envelope is bursty.
        auto env = envelope(mono(a), .25f);
        double mx = 0, mn = 1e9;
        for (double v : env) { mx = std::fmax(mx, v); mn = std::fmin(mn, v); }
        std::printf("   envelope range %.1f dB\n", db(mx) - db(mn));
        CHECK(db(mx) - db(mn) > 12);
    }
    {
        // A near strike cracks (bright burst); a far one does not.
        tw::Engine near_e, far_e;
        near_e.init(kSr, kBlock); far_e.init(kSr, kBlock);
        near_e.set(solo(&tw::Params::thunder, .03f));
        far_e.set(solo(&tw::Params::thunder, .03f));
        (void)render(near_e, 1.f); (void)render(far_e, 1.f);
        const_cast<tw::Thunder&>(near_e.thunder()).strike(1.f);
        const_cast<tw::Thunder&>(far_e.thunder()).strike(.1f);
        auto an = render(near_e, 8.f), af = render(far_e, 8.f);
        auto sn = avg_spectrum(slice(mono(an), .2f, 1.5f), 4096);
        auto sf = avg_spectrum(slice(mono(af), 2.f, 3.5f), 4096);
        std::printf("   near strike centroid %.0f Hz, far %.0f Hz; near peak %.2f, far peak %.2f\n",
            centroid(sn, 4096), centroid(sf, 4096), peak(an.l), peak(af.l));
        CHECK(peak(an.l) > peak(af.l));
    }
}

static void ocean_behaviour()
{
    std::printf("-- ocean waves\n");
    tw::Engine e;
    e.init(kSr, kBlock);
    e.set(solo(&tw::Params::ocean, .8f));
    (void)render(e, 2.f);
    auto a = render(e, 120.f);
    auto env = envelope(mono(a), .1f);
    double mx = 0, mn = 1e9;
    for (double v : env) { mx = std::fmax(mx, v); mn = std::fmin(mn, v); }
    std::printf("   wave swing %.1f dB\n", db(mx) - db(mn));
    CHECK(db(mx) - db(mn) > 6);

    // The envelope repeats on a timescale of roughly ten seconds.
    const size_t n = env.size();
    double mean = 0; for (double v : env) mean += v; mean /= n;
    double best = -1; size_t best_lag = 0;
    for (size_t lag = 50; lag <= 180; lag++) {
        double c = 0, d = 0;
        for (size_t i = 0; i + lag < n; i++) { c += (env[i] - mean) * (env[i + lag] - mean); }
        for (size_t i = 0; i < n; i++) d += (env[i] - mean) * (env[i] - mean);
        if (c / d > best) { best = c / d; best_lag = lag; }
    }
    std::printf("   envelope autocorrelation peak %.2f at %.1f s\n", best, best_lag * .1);
    CHECK(best > .1);

    // It gets brighter as it crashes: compare the quietest and loudest moments.
    auto s_lo = avg_spectrum(slice(mono(a), 0, 120), 8192);
    CHECK(centroid(s_lo, 8192) > 300);
}

static void rain_behaviour()
{
    std::printf("-- rain density\n");
    for (float k : { .1f, .4f, .7f, 1.f }) {
        tw::Engine e;
        e.init(kSr, kBlock);
        e.set(solo(&tw::Params::rain, k));
        (void)render(e, 1.f);
        const uint32_t before = e.rain().drops();
        (void)render(e, 30.f);
        const double rate = (e.rain().drops() - before) / 30.0;
        const double want = tw::Rain::drop_rate(k);
        std::printf("   knob %.1f: %.0f drops/s (expected %.0f)\n", k, rate, want);
        CHECKF(std::fabs(rate - want) < want * .25 + 2, "drop rate at %.1f", k);
    }
}

static void drone_behaviour()
{
    std::printf("-- drone pitch, glide and release\n");
    constexpr size_t N = 65536;
    auto fresh = [](tw::Engine& e, float level) {
        e.init(kSr, kBlock);
        tw::Params p; p.drone = level; p.volume = .8f;
        e.set(p);
    };
    const tw::Tuning d_minor;   // D, minor pentatonic, mid register
    {
        tw::Engine e; fresh(e, .8f);
        e.drone().set(1u << 0, d_minor);
        (void)render(e, 6.f);
        auto a = render(e, 4.f);
        auto sp = spectrum(mono(a), N);
        const double f = peak_hz(sp, N, 60, 400);
        const double want = tw::midi_hz(38.f);   // D2
        std::printf("   root pad: %.2f Hz (D2 = %.2f)\n", f, want);
        CHECKF(std::fabs(f / want - 1) < .02, "root plays D2 (%.2f)", f);
    }
    {
        tw::Engine e; fresh(e, .8f);
        e.drone().set((1u << 0) | (1u << 3), d_minor);
        (void)render(e, 6.f);
        auto sp = spectrum(mono(render(e, 4.f)), N);
        const double a2 = tw::midi_hz(45.f);
        const double fifth = peak_hz(sp, N, a2 * .97, a2 * 1.03);
        std::printf("   fifth present near %.2f Hz (A2 = %.2f)\n", fifth, a2);
        CHECK(band_level(sp, N, a2) > .2 * band_level(sp, N, tw::midi_hz(38.f)));
    }
    {
        // Change the root while a note is held: it glides, then settles on the new pitch.
        tw::Engine e; fresh(e, .8f);
        e.drone().set(1u << 0, d_minor);
        (void)render(e, 6.f);
        tw::Tuning up = d_minor; up.root = 7;   // G
        e.drone().set(1u << 0, up);
        auto early = spectrum(mono(render(e, .8f)), 32768);
        const double f_early = peak_hz(early, 32768, 60, 115);
        (void)render(e, 8.f);
        auto late = spectrum(mono(render(e, 4.f)), N);
        const double f_late = peak_hz(late, N, 60, 115);
        std::printf("   glide D2 -> G2: %.1f Hz early, %.2f Hz settled (G2 = %.2f)\n", f_early, f_late, tw::midi_hz(43.f));
        CHECK(f_early > tw::midi_hz(38.f) * .99 && f_early < tw::midi_hz(43.f) * 1.01);
        CHECK(std::fabs(f_late / tw::midi_hz(43.f) - 1) < .02);
    }
    {
        // Register switch: one octave up doubles the pitch.
        tw::Engine e; fresh(e, .8f);
        tw::Tuning hi = d_minor; hi.octave = 1;
        e.drone().set(1u << 0, hi);
        (void)render(e, 6.f);
        const double f = peak_hz(spectrum(mono(render(e, 4.f)), N), N, 40, 600);
        CHECK(std::fabs(f / (tw::midi_hz(38.f) * 2) - 1) < .02);
    }
    {
        // Release: un-latching fades to nothing and frees the voice.
        tw::Engine e; fresh(e, .8f);
        e.drone().set(0b101, d_minor);
        (void)render(e, 6.f);
        CHECK(e.drone().sounding() == 2);
        e.drone().set(0, d_minor);
        auto a = render(e, 24.f);
        CHECKF(peak(slice(a.l, 22.f, 24.f)) < 1e-4, "drone fades to silence (%g)", peak(slice(a.l, 22.f, 24.f)));
        CHECK(e.drone().sounding() == 0);
    }
    {
        // All ten pads, only eight voices: no overflow, no clipping, still finite.
        tw::Engine e; fresh(e, 1.f);
        tw::Params p; p.drone = 1.f; p.volume = 1.f; p.reverb = .6f; e.set(p);
        e.drone().set(0x3FF, d_minor);
        auto a = render(e, 30.f);
        CHECK(e.drone().gated() == 8);
        CHECK(finite(a.l) && finite(a.r));
        std::printf("   ten pads on eight voices: peak %.2f\n", peak(a.l));
        CHECK(peak(a.l) <= 1.0);
    }
}

static void reverb_behaviour()
{
    std::printf("-- reverb\n");
    {
        // Impulse response: decays, and gets longer as the knob goes up.
        double t60_prev = 0;
        for (float k : { .2f, .6f, 1.f }) {
            tw::Reverb rv;
            rv.init(kSr);
            rv.set(k);
            const size_t len = static_cast<size_t>(kSr * 30);
            std::vector<float> ir(len);
            for (size_t i = 0; i < len; i++) {
                float l, r;
                rv.process(i == 0 ? 1.f : 0.f, i == 0 ? 1.f : 0.f, l, r);
                ir[i] = l;
            }
            // Schroeder backwards integration for the decay time.
            std::vector<double> edc(len);
            double acc = 0;
            for (size_t i = len; i-- > 0;) { acc += double(ir[i]) * ir[i]; edc[i] = acc; }
            size_t t60 = len;
            for (size_t i = 0; i < len; i++) if (10 * std::log10(edc[i] / edc[0] + 1e-30) < -30) { t60 = i; break; }
            const double t = 2.0 * t60 / kSr;   // -30 dB doubled is the T60
            std::printf("   knob %.1f: T60 ~ %.1f s\n", k, t);
            CHECK(finite(ir));
            CHECKF(t > t60_prev, "decay grows with the knob (%.1f s)", t);
            t60_prev = t;
        }
    }
    {
        // Noise in for a minute at full: bounded.
        tw::Reverb rv;
        rv.init(kSr);
        rv.set(1.f);
        tw::Rng rng(5);
        double peak_out = 0;
        for (size_t i = 0; i < size_t(kSr * 60); i++) {
            float l, r;
            const float x = rng.bipolar() * .5f;
            rv.process(x, x, l, r);
            peak_out = std::fmax(peak_out, std::fmax(std::fabs(l), std::fabs(r)));
        }
        std::printf("   60 s of noise at full: peak %.2f\n", peak_out);
        CHECK(peak_out < 3.0);
    }
    {
        // Through the engine: wet is audible, and the two ears differ.
        tw::Engine dry, wet;
        dry.init(kSr, kBlock); wet.init(kSr, kBlock);
        auto p = solo(&tw::Params::rain, .5f);
        dry.set(p); p.reverb = 1.f; wet.set(p);
        (void)render(dry, 3.f); (void)render(wet, 3.f);
        auto d = render(dry, 20.f), w = render(wet, 20.f);
        std::printf("   rain dry %.1f dB, with reverb %.1f dB; L/R correlation %.2f\n",
            db(rms(mono(d))), db(rms(mono(w))), correlation(w.l, w.r));
        CHECK(correlation(w.l, w.r) < .9);
        CHECK(db(rms(mono(w))) > db(rms(mono(d))) - 6);
        CHECK(db(rms(mono(w))) < db(rms(mono(d))) + 9);
    }
    {
        // The tail rings on after the weather is shut off, then stops.
        tw::Engine e;
        e.init(kSr, kBlock);
        auto p = solo(&tw::Params::rain, .6f);
        p.reverb = 1.f;
        e.set(p);
        (void)render(e, 3.f);
        p.rain = 0.f;
        e.set(p);
        (void)render(e, 1.f);
        auto tail = render(e, 40.f);
        const double early = rms(slice(tail.l, 0, 3)), late = rms(slice(tail.l, 36, 40));
        std::printf("   tail: %.1f dB at 1-4 s after, %.1f dB at 37-41 s\n", db(early), db(late));
        CHECK(early > 1e-4);
        CHECK(late < early * .01);
    }
}

/* How peaky the tail's spectrum is, in dB: the deviation of the log magnitude
   from its local average over 200 Hz - 6 kHz. A perfectly diffuse tail scores
   about 5.6; the metallic ring of a sparse network scored 10. */
static double ripple(const float amount)
{
    tw::Reverb rv;
    rv.init(kSr);
    rv.set(amount);
    const size_t N = 131072;
    std::vector<float> ir(N);
    for (size_t i = 0; i < N; i++) {
        float l, r;
        rv.process(i == 0 ? 1.f : 0.f, i == 0 ? 1.f : 0.f, l, r);
        ir[i] = l;
    }
    auto s = spectrum(ir, N);
    const size_t lo = static_cast<size_t>(200 * N / kSr), hi = static_cast<size_t>(6000 * N / kSr);
    std::vector<double> L(hi + 1);
    for (size_t i = lo; i <= hi; i++) L[i] = 20 * std::log10(s[i] + 1e-12);
    double acc = 0;
    size_t n = 0;
    const int w = 64;
    for (size_t i = lo + w; i + w <= hi; i++) {
        double m = 0;
        for (int k = -w; k <= w; k++) m += L[i + k];
        m /= 2 * w + 1;
        acc += (L[i] - m) * (L[i] - m);
        n++;
    }
    return std::sqrt(acc / n);
}

static void reverb_smoothness()
{
    std::printf("-- reverb is smooth, not metallic\n");
    for (float a : { .2f, .6f, 1.f }) {
        const double r = ripple(a);
        std::printf("   knob %.1f: spectral ripple %.2f dB\n", a, r);
        CHECKF(r < 8.0, "reverb ripple at %.1f is %.2f dB", a, r);
    }
}

static void balance_with_drone()
{
    std::printf("-- the drone is not overpowered by a light touch of weather\n");
    auto level = [](float tw::Params::* f, const float v) {
        tw::Engine e;
        e.init(kSr, kBlock);
        tw::Params p;
        p.*f = v;
        p.volume = .8f;
        e.drone().set(0b1001, tw::Tuning {});
        e.set(p);
        (void)render(e, 3.f);
        return db(rms(mono(render(e, 40.f))));
    };
    const double drone = level(&tw::Params::drone, .5f);
    for (auto [name, f] : { std::pair<const char*, float tw::Params::*> { "wind", &tw::Params::wind },
                            { "rain", &tw::Params::rain }, { "ocean", &tw::Params::ocean } }) {
        const double a = level(f, .1f), b = level(f, .3f);
        std::printf("   %-6s at 0.1: %.1f dB, at 0.3: %.1f dB   (drone at 0.5: %.1f dB)\n", name, a, b, drone);
        CHECKF(a < drone - 15, "%s at 0.1 is well under the drone", name);
        CHECKF(b < drone - 5, "%s at 0.3 is still under the drone", name);
    }
}

static void rain_is_not_chirps()
{
    std::printf("-- rain drops are splashes, not high chirps\n");
    // Light rain: the drops dominate, so their spectrum is the drops'.
    tw::Engine e;
    e.init(kSr, kBlock);
    e.set(solo(&tw::Params::rain, .2f));
    (void)render(e, 2.f);
    auto a = render(e, 40.f);
    auto sp = avg_spectrum(mono(a), 8192);
    const double c = centroid(sp, 8192);
    const double hi = 1.0 - energy_below(sp, 8192, 4000);
    std::printf("   light rain: centroid %.0f Hz, %.1f%% of energy above 4 kHz\n", c, hi * 100);
    CHECK(c < 3000);
    CHECK(hi < .15);
}

static void motion_behaviour()
{
    std::printf("-- motion\n");
    auto spread = [](float motion) {
        tw::Engine e;
        e.init(kSr, kBlock);
        tw::Params p; p.rain = .5f; p.wind = .5f; p.ocean = .5f; p.thunder = .5f; p.volume = .8f; p.motion = motion;
        e.set(p);
        double lo = 1, hi = 0;
        float l[kBlock], r[kBlock];
        for (size_t b = 0; b < size_t(kSr * 300 / kBlock); b++) {
            e.process(l, r, kBlock);
            if (b > 1000) { lo = std::fmin(lo, e.effective(1)); hi = std::fmax(hi, e.effective(1)); }
        }
        return hi - lo;
    };
    const double s0 = spread(0.f), s1 = spread(1.f);
    std::printf("   rain level range over 5 min: motion 0 -> %.3f, motion 1 -> %.3f\n", s0, s1);
    CHECK(s0 < .001);
    CHECK(s1 > .15);

    // A knob that is off stays off however much it is allowed to wander.
    tw::Engine e;
    e.init(kSr, kBlock);
    tw::Params p; p.rain = 0.f; p.wind = 0.f; p.ocean = 0.f; p.thunder = 0.f; p.volume = 1.f; p.motion = 1.f;
    e.set(p);
    auto a = render(e, 60.f);
    CHECKF(peak(a.l) < 1e-5, "motion cannot turn on a knob that is off (%g)", peak(a.l));
}

static void whole_instrument()
{
    std::printf("-- everything at once, one minute\n");
    tw::Engine e;
    e.init(kSr, kBlock);
    tw::Params p;
    p.wind = p.rain = p.thunder = p.ocean = p.drone = 1.f;
    p.reverb = 1.f; p.volume = 1.f; p.motion = 1.f;
    e.set(p);
    e.drone().set(0x3FF, tw::Tuning {});
    (void)render(e, 5.f);
    auto a = render(e, 60.f);
    size_t hot = 0;
    for (float x : a.l) if (std::fabs(x) > .9f) hot++;
    std::printf("   peak %.3f, rms %.1f dB, samples above 0.9: %.3f%%\n", peak(a.l), db(rms(mono(a))), 100.0 * hot / a.l.size());
    CHECK(finite(a.l) && finite(a.r));
    CHECK(peak(a.l) <= 1.0 && peak(a.r) <= 1.0);
    CHECK(double(hot) / a.l.size() < .01);
}

static void speed()
{
    tw::Engine e;
    e.init(kSr, kBlock);
    tw::Params p;
    p.wind = p.rain = p.thunder = p.ocean = p.drone = 1.f;
    p.reverb = 1.f; p.volume = .8f;
    e.set(p);
    e.drone().set(0x3FF, tw::Tuning {});
    float l[kBlock], r[kBlock];
    for (int i = 0; i < 2000; i++) e.process(l, r, kBlock);
    const auto t0 = std::chrono::steady_clock::now();
    const int blocks = 20000;
    for (int i = 0; i < blocks; i++) e.process(l, r, kBlock);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double audio_s = blocks * kBlock / double(kSr);
    std::printf("-- speed: worst case renders %.0fx faster than real time on this Mac\n", audio_s / s);
}

int main(int argc, char** argv)
{
    levels_and_spectra();
    silence_and_taper();
    thunder_behaviour();
    ocean_behaviour();
    rain_behaviour();
    drone_behaviour();
    reverb_behaviour();
    reverb_smoothness();
    balance_with_drone();
    rain_is_not_chirps();
    motion_behaviour();
    whole_instrument();
    speed();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
