#include "touch2.h"

#include <algorithm>
#include "ui/layout.h"

using namespace tw;
using namespace daisy;

void Touch2::init(DaisySeed& hw)
{
    AdcChannelConfig cfg[PanelInput::KnobCount];
    cfg[0].InitSingle(seed::A0);
    cfg[1].InitSingle(seed::A1);
    cfg[2].InitSingle(seed::A2);
    cfg[3].InitSingle(seed::A3);
    cfg[4].InitSingle(seed::A4);
    cfg[5].InitSingle(seed::A5);
    cfg[6].InitSingle(seed::A6);
    cfg[7].InitSingle(seed::A7);
    hw.adc.Init(cfg, PanelInput::KnobCount);
    hw.adc.Start();

    _sw_a.Init(seed::D7, seed::D6);
    _sw_b.Init(seed::D9, seed::D8);

    _init_mpr121();

    // Let the touch controller's baseline tracking settle before anything
    // starts treating its output as a finger. TouchString does the same.
    for (int i = 0; i < 40; i++) {
        _read_pads();
        System::Delay(3);
    }
    _seen = 0;

    // And the ADC's smoothing: start from where the pots really are.
    System::Delay(20);
    service(hw);
    service(hw);
}

void Touch2::_write(const uint8_t reg, const uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    _i2c.TransmitBlocking(kMpr121Addr, buf, 2, 10);
}

bool Touch2::_read(const uint8_t start, uint8_t* buf, const uint16_t size)
{
    uint8_t reg = start;
    if (_i2c.TransmitBlocking(kMpr121Addr, &reg, 1, 10) != I2CHandle::Result::OK) return false;
    return _i2c.ReceiveBlocking(kMpr121Addr, buf, size, 10) == I2CHandle::Result::OK;
}

/* Register values are TouchString's, which are tuned for this board's pads. */
void Touch2::_init_mpr121()
{
    I2CHandle::Config conf;
    conf.mode = I2CHandle::Config::Mode::I2C_MASTER;
    conf.periph = I2CHandle::Config::Peripheral::I2C_1;
    conf.speed = I2CHandle::Config::Speed::I2C_400KHZ;
    conf.pin_config.scl = Pin(PORTB, 8);
    conf.pin_config.sda = Pin(PORTB, 9);
    _i2c.Init(conf);

    _write(0x80, 0x63);            // soft reset
    System::Delay(5);
    _write(0x5E, 0x00);            // stop mode, to configure

    for (uint8_t i = 0; i < kElectrodes; i++) {
        _write(0x41 + i * 2, 6);   // touch threshold
        _write(0x42 + i * 2, 3);   // release threshold
    }

    // Baseline filters (NXP AN3891): quick to rise, slow to fall, frozen
    // while touched, so a finger is not absorbed into the baseline.
    _write(0x2B, 0x01); _write(0x2C, 0x01); _write(0x2D, 0x0E); _write(0x2E, 0x00);
    _write(0x2F, 0x01); _write(0x30, 0x01); _write(0x31, 0x10); _write(0x32, 0x04);
    _write(0x33, 0x00); _write(0x34, 0x00); _write(0x35, 0x00);

    _write(0x5B, 0x11);            // debounce: one sample to touch, one to release
    _write(0x5C, 0xD0);            // AFE config 1: 34 filter iterations, 16 uA
    _write(0x5D, 0x51);            // AFE config 2

    _write(0x7D, 200);             // auto-configuration limits
    _write(0x7E, 130);
    _write(0x7F, 180);
    _write(0x7B, 0xCB);
    _write(0x7C, 0x00);

    _write(0x5E, 0x8C);            // run: 12 electrodes, baseline tracking on
    System::Delay(80);
}

bool Touch2::_read_pads()
{
    uint8_t raw[42];
    if (!_read(0x00, raw, sizeof raw)) {
        if (_i2c_fails < 255) _i2c_fails ++;
        return false;
    }
    _i2c_fails = 0;

    auto reported = static_cast<uint16_t>((raw[1] & 0x0F) << 8) | raw[0];

    for (uint8_t i = 0; i < kElectrodes; i++) {
        auto mask = static_cast<uint16_t>(1u << i);
        bool touched = reported & mask;
        bool was = _state & mask;

        // Anti-stuck guard (TouchString's): whatever the status register
        // says, if the filtered reading is back at the baseline the pad is
        // not being touched.
        auto filtered = static_cast<int32_t>(((raw[4 + i * 2 + 1] & 0x03) << 8) | raw[4 + i * 2]);
        auto baseline = static_cast<int32_t>(raw[0x1E + i]) << 2;
        if (baseline - filtered < 3) touched = false;

        if (_lockout[i] > 0) _lockout[i] --;

        if (touched != was) {
            if (touched) {
                if (_lockout[i] == 0) { _state |= mask; _debounce[i] = 0; }
            }
            else if (++_debounce[i] >= 2) {
                _debounce[i] = 0;
                _state &= ~mask;
                _lockout[i] = 3;
            }
        }
        else {
            _debounce[i] = 0;
        }
    }
    return true;
}

void Touch2::service(DaisySeed& hw)
{
    // Analog inputs, lightly smoothed and trimmed so the ends of the travel
    // actually reach 0 and 1.
    for (uint8_t i = 0; i < PanelInput::KnobCount; i++) {
        auto raw = hw.adc.GetFloat(i);
        if (layout::kInvert[i]) raw = 1.f - raw;
        _smooth[i] = _knobs_primed ? _smooth[i] + (raw - _smooth[i]) * .35f : raw;
        _knob[i] = std::clamp((_smooth[i] - .004f) / .992f, 0.f, 1.f);
    }
    _knobs_primed = true;

    _sw[0] = static_cast<uint8_t>(_sw_a.Read());
    _sw[1] = static_cast<uint8_t>(_sw_b.Read());

    if (_read_pads()) {
        _pads = _state;
        _seen.fetch_or(_state);
    }
    // One missed transfer is noise; twenty in a row is a dead controller.
    _pads_ok = _i2c_fails < 20;
    if (!_pads_ok) _pads = 0;
}

void Touch2::snapshot(PanelInput& in)
{
    for (uint8_t i = 0; i < PanelInput::KnobCount; i++) in.knob[i] = _knob[i];
    in.sw[0] = _sw[0];
    in.sw[1] = _sw[1];
    in.pads = _pads;
    in.pads_seen = _seen.exchange(0);
    in.pads_ok = _pads_ok;
}
