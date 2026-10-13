#include "app.h"

#include "daisy_seed.h"
#include "dsp/engine.h"
#include "hw/seed.led.h"
#include "hw/touch2.h"
#include "ui/weather.ui.h"

using namespace daisy;

namespace tw {

/*
Everything runs from the audio callback: it reads the panel snapshot, updates
the UI, then renders a block. The UI therefore never races the DSP. The only
other things going on are the main loop, which does the blocking I2C read of
the pads, and a timer that PWMs the LED.
*/
class AppImpl {
public:
    AppImpl(): _ui { _engine } {}

    void Init();
    void Loop();
    void Audio(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size);
    void Timer() { _led.update(); }

private:
#if TW_DEBUG
    void _log();
#endif

    DaisySeed  _hw;
    SeedLed    _led;
    Touch2     _touch;
    Engine     _engine;
    WeatherUI  _ui;
    PanelInput _panel;

#if TW_DEBUG
    CpuLoadMeter _cpu;
    uint32_t     _log_t { 0 };
    uint16_t     _last_pads { 0 };
    GPIO         _scan[32];
    uint32_t     _scan_valid { 0 }, _last_scan { 0xffffffff };
#endif
};

}

using namespace tw;

static AppImpl impl;

static TimerHandle tim5;

static void TimerCallback(void*) { impl.Timer(); }

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    impl.Audio(in, out, size);
}

void AppImpl::Init()
{
    // 48 samples at 48 kHz: the UI polls at 1 kHz.
    constexpr size_t kBlock = 48;

    _hw.Init(true);   // boost to 480 MHz
    _hw.SetAudioBlockSize(kBlock);
    _hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    _led.init(_hw);

    // The board first: the UI adopts the panel as it finds it.
    _touch.init(_hw);
    _touch.snapshot(_panel);

    _engine.init(_hw.AudioSampleRate(), kBlock);
    _ui.init(_panel);

#if TW_DEBUG
    _hw.StartLog(false);
    System::Delay(1500);   // give the host time to open the port
    _hw.PrintLine("[app] TouchWeather, debug build");
    _cpu.Init(_hw.AudioSampleRate(), kBlock);

    // Pin scanner: every free digital header pin as a pulled-up input, so the
    // switches can be found by flipping them. Skips the pins the board uses
    // (I2C D11/D12, ADC D15-D22) and USB (D29/D30).
    for (int d = 0; d <= 28; d++) {
        if (d == 11 || d == 12 || (d >= 15 && d <= 22)) continue;
        _scan[d].Init(_hw.GetPin(d), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
        _scan_valid |= 1u << d;
    }
#endif

    _hw.StartAudio(AudioCallback);

    // The LED's software PWM runs at SeedLed::kUpdateRate. Per GetPClk2Freq's
    // docs the timers run at twice their peripheral frequency.
    TimerHandle::Config cfg;
    cfg.periph = TimerHandle::Config::Peripheral::TIM_5;
    cfg.dir = TimerHandle::Config::CounterDir::UP;
    cfg.period = System::GetPClk2Freq() / (SeedLed::kUpdateRate / 2);
    cfg.enable_irq = true;
    tim5.Init(cfg);
    tim5.SetCallback(TimerCallback);
    tim5.Start();

    _led.self_test();
}

void AppImpl::Audio(AudioHandle::InputBuffer, AudioHandle::OutputBuffer out, size_t size)
{
#if TW_DEBUG
    _cpu.OnBlockStart();
#endif
    _touch.snapshot(_panel);
    _ui.poll(_panel, static_cast<float>(size) / 48000.f);
    _led.set(_ui.led());
    _engine.process(out[0], out[1], size);
#if TW_DEBUG
    _cpu.OnBlockEnd();
#endif
}

void AppImpl::Loop()
{
    while (true) {
        _touch.service(_hw);
#if TW_DEBUG
        _log();
#endif
    }
}

#if TW_DEBUG
/* Every pad as it is touched (to map electrodes to roles in ui/layout.h), and
   once a second the whole panel and the CPU. */
void AppImpl::_log()
{
    const auto pads = _panel.pads;
    if (pads != _last_pads) {
        _hw.PrintLine("[pad] electrodes 0x%03x", static_cast<unsigned>(pads));
        _last_pads = pads;
    }

    uint32_t scan = 0;
    for (int d = 0; d <= 28; d++) if ((_scan_valid >> d) & 1 && _scan[d].Read()) scan |= 1u << d;
    if (scan != _last_scan) {
        _hw.PrintLine("[gpio] high 0x%08x  (changed 0x%08x)", static_cast<unsigned>(scan), static_cast<unsigned>(scan ^ _last_scan));
        _last_scan = scan;
    }

    const auto now = System::GetNow();
    if (now - _log_t < 1000) return;
    _log_t = now;

    _hw.PrintLine("[panel] swA %d swB %d pads_ok %d | A0/P1 %d A1/P2 %d A2/P3 %d A3/P4 %d A4/P5 %d A5/P6 %d | A6/S1 %d A7/S2 %d",
        _panel.sw[0], _panel.sw[1], static_cast<int>(_panel.pads_ok),
        static_cast<int>(_panel.knob[0] * 1000), static_cast<int>(_panel.knob[1] * 1000),
        static_cast<int>(_panel.knob[2] * 1000), static_cast<int>(_panel.knob[3] * 1000),
        static_cast<int>(_panel.knob[4] * 1000), static_cast<int>(_panel.knob[5] * 1000),
        static_cast<int>(_panel.knob[6] * 1000), static_cast<int>(_panel.knob[7] * 1000));
    _hw.PrintLine("[state] latched 0x%03x ghosts 0x%03x root %d scale %d next-oct %d | cpu avg %d%% max %d%%",
        _ui.latched(), _ui.ghosts(), _ui.tuning().root, _ui.tuning().scale, _ui.next_octave(),
        static_cast<int>(_cpu.GetAvgCpuLoad() * 100), static_cast<int>(_cpu.GetMaxCpuLoad() * 100));
    _cpu.Reset();
}
#endif

void Application::Init() { impl.Init(); }
void Application::Loop() { impl.Loop(); }
