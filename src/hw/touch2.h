#pragma once

#include <atomic>
#include <array>

#include "daisy_seed.h"
#include "ui/panel.h"
#include "nocopy.h"

namespace tw {

/*
The Synthux Touch 2 board, read into a PanelInput.

Pins and the MPR121 setup are taken from Synthux Academy's TouchString
firmware, which runs on the same board:

    knobs and faders  A0..A7, straight to the ADC (no multiplexer)
    switch A          D7 / D6   (Switch3)
    switch B          D9 / D8   (Switch3)
    pads              MPR121 on I2C1, SCL PB8, SDA PB9, 400 kHz, polled

The I2C transfer is blocking and cannot run from an interrupt, so the work is
split in two: service() runs in the main loop and reads the board, and
snapshot() runs wherever the UI does and just copies out what was last read.
*/
class Touch2 {
public:
    Touch2() = default;
    ~Touch2() = default;

    void init(daisy::DaisySeed&);

    /* Main loop, as often as it will go (the I2C read paces it). */
    void service(daisy::DaisySeed&);

    /* Any context. Clears the "touched since last time" latch. */
    void snapshot(PanelInput&);

private:
    NOCOPY(Touch2)

    static constexpr uint8_t kMpr121Addr = 0x5A;
    static constexpr uint8_t kElectrodes = 12;

    void _write(const uint8_t reg, const uint8_t val);
    bool _read(const uint8_t start, uint8_t* buf, const uint16_t size);
    void _init_mpr121();
    bool _read_pads();

    daisy::I2CHandle _i2c;
    daisy::Switch3 _sw_a, _sw_b;

    std::array<float, PanelInput::KnobCount> _smooth {};
    bool _knobs_primed { false };

    uint16_t _state { 0 };
    std::array<uint8_t, kElectrodes> _debounce {};
    std::array<uint8_t, kElectrodes> _lockout {};
    uint8_t _i2c_fails { 0 };

    /* Written by service() in the main loop, read by snapshot(). Each field
       is a single word, so a reader may mix two passes but never tears one. */
    volatile float    _knob[PanelInput::KnobCount] {};
    volatile uint8_t  _sw[2] {};
    volatile uint16_t _pads { 0 };
    volatile bool     _pads_ok { false };
    std::atomic<uint16_t> _seen { 0 };
};

}
