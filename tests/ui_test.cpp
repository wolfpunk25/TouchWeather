#include "harness.h"
#include "ui/weather.ui.h"

using namespace th;
using tw::PanelInput;

namespace {

struct Rig {
    tw::Engine engine;
    tw::WeatherUI ui { engine };
    PanelInput in;
    static constexpr float kDt = 1.f / 1000.f;

    Rig()
    {
        engine.init(kSr, kBlock);
        in.knob.fill(0.f);
        in.knob[PanelInput::FaderVolume] = .8f;
        ui.init(in);
    }
    void poll(const int n = 1) { for (int i = 0; i < n; i++) { ui.poll(in, kDt); in.pads_seen = in.pads; } }
    /* A tap seen only by the "touched since last poll" latch, as a quick tap is. */
    void quick_tap(const int electrode) { in.pads_seen |= 1u << electrode; ui.poll(in, kDt); in.pads_seen = in.pads; ui.poll(in, kDt); }
    void hold(const int electrode, const int polls = 60) { in.pads |= 1u << electrode; poll(polls); in.pads &= ~(1u << electrode); poll(5); }
    void run(const float seconds)
    {
        float l[kBlock], r[kBlock];
        for (int i = 0; i < int(seconds * 1000); i++) { poll(); engine.process(l, r, kBlock); }
    }
};

void pads()
{
    std::printf("-- pads\n");
    Rig t;
    CHECK(t.ui.latched() == 0b1001);   // root and fifth to begin with

    t.hold(2);
    CHECK(t.ui.latched() == 0b1101);
    t.hold(2);
    CHECK(t.ui.latched() == 0b1001);

    // A touch that begins and ends between polls still counts, once.
    t.quick_tap(5);
    CHECK(t.ui.latched() == 0b101001);
    t.quick_tap(5);
    CHECK(t.ui.latched() == 0b1001);

    // Holding a pad for a long time toggles it once, not every poll.
    t.hold(7, 2000);
    CHECK(t.ui.latched() == 0b10001001);

    t.hold(10);   // clear
    CHECK(t.ui.latched() == 0);
    t.hold(1);
    CHECK(t.ui.latched() == 0b10);
}

void shift_and_switches()
{
    std::printf("-- shift, scale and register\n");
    Rig t;
    CHECK(t.ui.tuning().root == 2);

    // Shift alone does nothing audible.
    t.hold(11);
    CHECK(t.ui.latched() == 0b1001);
    CHECK(t.ui.tuning().root == 2);

    // Shift held, then a pad: that pad sets the root and does not latch.
    t.in.pads = 1u << 11; t.poll(20);
    t.in.pads |= 1u << 7; t.poll(20);
    t.in.pads = 1u << 11; t.poll(20);
    CHECK(t.ui.tuning().root == 7);
    CHECK(t.ui.latched() == 0b1001);
    t.in.pads = 0; t.poll(20);

    // And once shift is let go, pads latch notes again.
    t.hold(7);
    CHECK(t.ui.latched() == 0b10001001);
    CHECK(t.ui.tuning().root == 7);

    t.in.sw[0] = PanelInput::SwUp;   t.poll(); CHECK(t.ui.tuning().scale == 0);
    t.in.sw[0] = PanelInput::SwCentre; t.poll(); CHECK(t.ui.tuning().scale == 1);
    t.in.sw[0] = PanelInput::SwDown; t.poll(); CHECK(t.ui.tuning().scale == 2);
    t.in.sw[1] = PanelInput::SwUp;   t.poll(); CHECK(t.ui.tuning().octave == 1);
    t.in.sw[1] = PanelInput::SwDown; t.poll(); CHECK(t.ui.tuning().octave == -1);
    CHECK(t.ui.tuning().root == 7);   // switches leave the root alone

    // The drone actually follows: the root pad now sounds at G.
    Rig u;
    u.in.pads = 1u << 11; u.poll(10);
    u.in.pads |= 1u << 7; u.poll(10);
    u.in.pads = 0; u.poll(10);
    u.in.knob[PanelInput::KnobDrone] = .8f;
    u.run(14.f);
    float l[kBlock], r[kBlock];
    Audio a;
    for (int i = 0; i < int(4.f * kSr / kBlock); i++) { u.engine.process(l, r, kBlock); append(a, l, r, kBlock); }
    const double f = peak_hz(spectrum(mono(a), 65536), 65536, 60, 115);
    std::printf("   root pad after shift+7: %.2f Hz (G2 = %.2f)\n", f, tw::midi_hz(43.f));
    CHECK(std::fabs(f / tw::midi_hz(43.f) - 1) < .02);
}

void panel_to_engine()
{
    std::printf("-- panel to engine\n");
    Rig t;
    t.in.knob[PanelInput::KnobWind] = .3f;
    t.in.knob[PanelInput::KnobRain] = .6f;
    t.in.knob[PanelInput::KnobThunder] = .2f;
    t.in.knob[PanelInput::KnobOcean] = .9f;
    t.in.knob[PanelInput::KnobMotion] = 0.f;
    t.run(3.f);
    CHECK(std::fabs(t.engine.effective(0) - .3f) < .01f);
    CHECK(std::fabs(t.engine.effective(1) - .6f) < .01f);
    CHECK(std::fabs(t.engine.effective(2) - .2f) < .01f);
    CHECK(std::fabs(t.engine.effective(3) - .9f) < .01f);
}

void bad_touch_controller()
{
    std::printf("-- a dead touch controller\n");
    Rig t;
    t.in.pads_ok = false;
    t.in.pads = 0xFFF;
    t.in.pads_seen = 0xFFF;
    t.poll(100);
    CHECK(t.ui.latched() == 0b1001);   // garbage pads are ignored
}

void ghosts()
{
    std::printf("-- ghost notes\n");
    {
        Rig t;
        t.in.knob[PanelInput::KnobMotion] = 0.f;
        t.in.knob[PanelInput::KnobDrone] = .5f;
        for (int i = 0; i < 600000; i++) t.poll();
        CHECK(t.ui.ghosts() == 0);
    }
    {
        Rig t;
        t.in.knob[PanelInput::KnobMotion] = 1.f;
        t.in.knob[PanelInput::KnobDrone] = .5f;
        int max_ghosts = 0, changes = 0;
        uint16_t prev = 0;
        for (int i = 0; i < 1200000; i++) {
            t.poll();
            max_ghosts = std::max(max_ghosts, __builtin_popcount(t.ui.ghosts()));
            if (t.ui.ghosts() != prev) { changes++; prev = t.ui.ghosts(); }
            if (t.ui.latched() != 0b1001) { CHECK(false); break; }
            if (t.ui.ghosts() & t.ui.latched()) { CHECK(false); break; }
        }
        std::printf("   20 minutes at motion 1: %d ghost changes, at most %d at once\n", changes, max_ghosts);
        CHECK(max_ghosts >= 1 && max_ghosts <= 2);
        CHECK(changes >= 8);

        // Turn motion down and they leave; the user's notes stay.
        t.in.knob[PanelInput::KnobMotion] = 0.f;
        t.poll(5);
        CHECK(t.ui.ghosts() == 0);
        CHECK(t.ui.latched() == 0b1001);
    }
    {
        // Tapping a ghost sends it away rather than adopting it.
        Rig t;
        t.in.knob[PanelInput::KnobMotion] = 1.f;
        for (int i = 0; i < 400000 && !t.ui.ghosts(); i++) t.poll();
        CHECK(t.ui.ghosts() != 0);
        const int g = __builtin_ctz(t.ui.ghosts());
        t.hold(g);
        CHECK((t.ui.ghosts() & (1u << g)) == 0);
        CHECK((t.ui.latched() & (1u << g)) == 0);
    }
}

void led()
{
    std::printf("-- LED\n");
    Rig t;
    t.in.knob[PanelInput::KnobThunder] = 1.f;
    t.run(1.f);
    const float quiet = t.ui.led();
    CHECK(quiet >= 0.f && quiet < .3f);
    const_cast<tw::Thunder&>(t.engine.thunder()).strike(1.f);
    t.run(.05f);
    CHECK(t.ui.led() > .8f);
    t.hold(4, 5);
    CHECK(t.ui.led() >= 0.f && t.ui.led() <= 1.f);
}

} // namespace

int main()
{
    pads();
    shift_and_switches();
    panel_to_engine();
    bad_touch_controller();
    ghosts();
    led();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
