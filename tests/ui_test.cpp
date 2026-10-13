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

constexpr uint16_t kRootPads = (1u << 10) | (1u << 11);

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

    // Two note pads at once are two notes.
    t.in.pads = (1u << 4) | (1u << 6); t.poll(20); t.in.pads = 0; t.poll(20);
    CHECK(t.ui.latched() == 0b11011001);

    // The pads at both ends of the row are notes like any other.
    t.hold(0); t.hold(9);
    CHECK(t.ui.latched() == 0b1011011000);
}

void root_and_clear()
{
    std::printf("-- root pads and clear\n");
    {
        Rig t;
        CHECK(t.ui.tuning().root == 2);
        t.hold(11); CHECK(t.ui.tuning().root == 3);
        t.hold(11); CHECK(t.ui.tuning().root == 4);
        t.hold(10); CHECK(t.ui.tuning().root == 3);
        t.quick_tap(10); CHECK(t.ui.tuning().root == 2);
        CHECK(t.ui.latched() == 0b1001);   // root pads never latch a note

        for (int i = 0; i < 3; i++) t.hold(10);
        CHECK(t.ui.tuning().root == 11);   // 2 - 3 wraps below C
        for (int i = 0; i < 2; i++) t.hold(11);
        CHECK(t.ui.tuning().root == 1);    // and above B
    }
    {
        // Held together, they cancel out and then clear everything.
        Rig t;
        t.hold(4);
        CHECK(t.ui.latched() == 0b11001);
        t.in.pads = kRootPads; t.poll(100);
        CHECK(t.ui.tuning().root == 2);
        CHECK(t.ui.latched() == 0b11001);  // not yet
        t.poll(600);
        CHECK(t.ui.latched() == 0);
        t.in.pads = 0; t.poll(20);
        CHECK(t.ui.latched() == 0);
        t.hold(1);
        CHECK(t.ui.latched() == 0b10);
        // And again, on a second hold.
        t.in.pads = kRootPads; t.poll(800); t.in.pads = 0; t.poll(20);
        CHECK(t.ui.latched() == 0);
    }
    {
        // Held one at a time, however long, never clears.
        Rig t;
        t.in.pads = 1u << 10; t.poll(3000); t.in.pads = 0; t.poll(20);
        CHECK(t.ui.latched() == 0b1001);
    }
    {
        // A short touch of both is not a clear.
        Rig t;
        t.in.pads = kRootPads; t.poll(100); t.in.pads = 0; t.poll(20);
        CHECK(t.ui.latched() == 0b1001);
        CHECK(t.ui.tuning().root == 2);
    }
}

void switches()
{
    std::printf("-- scale, register and the drone following the root\n");
    Rig t;
    t.in.sw[0] = PanelInput::SwUp;     t.poll(); CHECK(t.ui.tuning().scale == 0);
    t.in.sw[0] = PanelInput::SwCentre; t.poll(); CHECK(t.ui.tuning().scale == 1);
    t.in.sw[0] = PanelInput::SwDown;   t.poll(); CHECK(t.ui.tuning().scale == 2);
    t.in.sw[1] = PanelInput::SwUp;     t.poll(); CHECK(t.ui.tuning().octave == 1);
    t.in.sw[1] = PanelInput::SwDown;   t.poll(); CHECK(t.ui.tuning().octave == -1);

    // The drone really does follow the root pads: D up five semitones is G.
    Rig u;
    for (int i = 0; i < 5; i++) u.hold(11);
    u.in.knob[PanelInput::KnobDrone] = .8f;
    u.run(14.f);
    float l[kBlock], r[kBlock];
    Audio a;
    for (int i = 0; i < int(4.f * kSr / kBlock); i++) { u.engine.process(l, r, kBlock); append(a, l, r, kBlock); }
    const double f = peak_hz(spectrum(mono(a), 65536), 65536, 60, 115);
    std::printf("   root pad after 5 steps up: %.2f Hz (G2 = %.2f)\n", f, tw::midi_hz(43.f));
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
    root_and_clear();
    switches();
    panel_to_engine();
    bad_touch_controller();
    ghosts();
    led();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
