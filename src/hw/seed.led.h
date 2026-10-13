#pragma once

#include <cmath>

#include "daisy_seed.h"
#include "nocopy.h"

namespace tw {

/*
The Seed's own LED is the only indicator the Touch 2 gives us, and the UI
asks a lot of it (brightness that follows the playhead, blinks, flickers), so
it is driven with a software PWM: kLevels steps per carrier cycle, one step per
call of update(), which runs at kLevels * kCarrierHz.

The carrier has to stay well above the flicker band. TouchString's LED was
visibly flickering at 120 Hz; this runs at 500 Hz.
*/
class SeedLed {
public:
    static constexpr uint32_t kCarrierHz = 500;
    static constexpr uint32_t kLevels = 32;
    static constexpr float kUpdateRate = kCarrierHz * kLevels;   // 16 kHz

    SeedLed() = default;
    ~SeedLed() = default;

    void init(daisy::DaisySeed& hw) { _hw = &hw; }

    /* The fraction of the time the LED is lit, 0..1. Anything above zero
       gets at least one step, so the UI's faint 0.04 "armed, between blinks"
       still glows rather than vanishing. */
    void set(const float duty)
    {
        auto d = duty < 0.f ? 0.f : (duty > 1.f ? 1.f : duty);
        auto steps = static_cast<uint8_t>(d * kLevels + .5f);
        if (d > .01f && steps == 0) steps = 1;
        _on_steps = steps;
    }

    void update()
    {
        _hw->SetLed(_step < _on_steps);
        if (++_step >= kLevels) _step = 0;
    }

    /* Three blinks at power-up: the LED, the timer and the PWM in one glance. */
    void self_test()
    {
        for (int i = 0; i < 3; i++) {
            set(1.f); daisy::System::Delay(120);
            set(0.f); daisy::System::Delay(120);
        }
    }

private:
    NOCOPY(SeedLed)

    daisy::DaisySeed* _hw { nullptr };
    volatile uint8_t _on_steps { 0 };
    uint8_t _step { 0 };
};

}
