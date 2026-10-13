/* Renders a few scenes through the real UI and engine to WAV files, so the
   instrument can be heard before it is flashed:  make -C tests render */
#include "harness.h"
#include "ui/weather.ui.h"

using namespace th;
using tw::PanelInput;

struct Scene {
    const char* name;
    float wind, rain, thunder, ocean, drone, motion, reverb, volume;
    int scale, octave;
    float seconds;
};

static Audio play(const Scene& s, const bool played)
{
    tw::Engine engine;
    engine.init(kSr, kBlock);
    tw::WeatherUI ui(engine);
    PanelInput in;
    in.knob = { s.wind, s.rain, s.thunder, s.ocean, s.drone, s.motion, s.reverb, s.volume };
    in.sw[0] = s.scale == 0 ? PanelInput::SwUp : s.scale == 2 ? PanelInput::SwDown : PanelInput::SwCentre;
    in.sw[1] = s.octave > 0 ? PanelInput::SwUp : s.octave < 0 ? PanelInput::SwDown : PanelInput::SwCentre;
    ui.init(in);

    Audio a;
    float l[kBlock], r[kBlock];
    const size_t blocks = static_cast<size_t>(s.seconds * kSr / kBlock);
    for (size_t b = 0; b < blocks; b++) {
        const float t = b * kBlock / kSr;
        if (played) {
            // A little performance: add notes, move the root, ease the weather in.
            auto tap = [&](float at, int electrode) {
                if (t >= at && t < at + .001f) { in.pads_seen |= 1u << electrode; }
            };
            tap(6, 1); tap(14, 4); tap(22, 6); tap(30, 8);
            if (t >= 40 && t < 40.1f) in.pads = 1u << 11;
            if (t >= 40.1f && t < 40.2f) { in.pads = (1u << 11) | (1u << 7); }
            if (t >= 40.2f) in.pads = 0;
            tap(52, 10);
            in.knob[PanelInput::KnobRain] = std::min(.7f, t / 60.f);
            in.knob[PanelInput::KnobWind] = std::min(.5f, t / 80.f);
        }
        ui.poll(in, kBlock / kSr);
        in.pads_seen = in.pads;
        engine.process(l, r, kBlock);
        append(a, l, r, kBlock);
    }
    return a;
}

int main()
{
    const Scene scenes[] = {
        // name          wind rain thun ocean drone mot  rev  vol  scale oct  secs
        { "calm_sea",    .25f, 0.f, 0.f, .65f, .40f, .40f, .55f, .8f, 0, 0, 120 },
        { "storm",       .80f, .8f, .75f, .50f, .30f, .60f, .50f, .8f, 1, -1, 120 },
        { "rainy_window",0.f, .55f, .30f, 0.f, .50f, .30f, .65f, .8f, 2, 0, 120 },
        { "moor",        .70f, 0.f, .15f, 0.f, .60f, .50f, .75f, .8f, 1, 0, 120 },
        { "played",      .20f, .2f, .20f, .40f, .55f, .30f, .60f, .8f, 0, 0, 90 },
    };
    for (const auto& s : scenes) {
        auto a = play(s, std::string(s.name) == "played");
        write_wav(std::string("build/") + s.name + ".wav", a);
        auto m = mono(a);
        std::printf("%-13s rms %6.1f dBFS  peak %.2f  L/R corr %.2f\n", s.name, db(rms(m)), std::fmax(peak(a.l), peak(a.r)), correlation(a.l, a.r));
    }
    return 0;
}
