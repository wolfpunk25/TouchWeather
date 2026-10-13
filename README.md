# TouchWeather

An ambient weather machine for the [Synthux Touch 2](https://synthux.academy) (Daisy Seed).
Four knobs dial in **wind, rain, thunder and ocean**; the pads latch **drone notes**
underneath. Play it, or set it and leave it: with *Motion* up the weather drifts by
itself and the drone occasionally lets a ghost note in and out, so it never loops.

Inspired by [mynoise.net](https://mynoise.net)'s noise machines. Everything is
synthesised (no samples), written from scratch as portable header-only C++ in
`src/dsp/`, so it is tested and listened to on the Mac before it is ever flashed.

## Controls

| Control | Does |
| --- | --- |
| Knob 1 (A0) | **Wind**: level, pitch and how gusty |
| Knob 2 (A1) | **Rain**: from a few drops a second to a downpour |
| Knob 3 (A2) | **Thunder**: from a distant bed of rumble to frequent, close strikes. The LED flashes with the lightning |
| Knob 4 (A3) | **Ocean**: two layers of surf on unrelated clocks, brightening as each wave breaks |
| Knob 5 (A4) | **Drone**: level, and with it brightness |
| Knob 6 (A5) | **Motion**: how far the weather wanders on its own (0 = exactly as set) |
| Left fader (A6) | **Reverb**: wet amount and tail length (1.5 s up to ~12 s) |
| Right fader (A7) | **Volume** |
| Switch A | scale: **up** major pentatonic (bright), **centre** minor pentatonic (dark), **down** suspended |
| Switch B | register: **up** +1 octave, **centre** mid, **down** -1 octave |
| Pads 1-10 | latch drone notes (tap to add, tap again to fade). Two octaves of the scale, from the root |
| Pad 11 | **release all** notes |
| Pad 12 | **shift**: hold it and tap pads 1-11 to set the root (C, C#, D ... A#) |

It starts on D with a root and fifth already sounding. Held notes glide to a new
root, scale or register rather than jumping. Five-note scales only, so any
combination of pads sounds like a chord.

The Seed's LED shows lightning, a faint glow that swells with the surf, and a
blink for every pad.

**Knob numbers follow the Seed's pins (A0-A7); faceplate labels differ between
boards.** If a fader runs backwards, or the pads aren't in the order above, see
[Hardware to check](#hardware-to-check).

## Build and flash

Needs the ARM GNU toolchain and `dfu-util` on `PATH`.

```bash
git clone --recursive https://github.com/wolfpunk25/TouchWeather.git
cd TouchWeather
make libs      # once
make
make program-dfu   # Seed in DFU mode: hold BOOT, tap RESET
```

110 KB of the Seed's 128 KB flash, so no bootloader is needed. This replaces
whatever is on the board (flash TouchString back from flash.daisy.audio any time).

## Hear it without hardware

```bash
make test                # DSP and UI tests, no hardware
make -C tests render     # writes five scenes to tests/build/*.wav
afplay tests/build/storm.wav
```

The scenes drive the real UI and engine, so they are the firmware's sound.

## Hardware to check

Not yet flashed or heard on the board.

1. **Pad order.** `make DEBUG=1`, open the Seed's USB serial port, touch each pad: it
   prints the electrode number. Put them in `kPad` in `src/ui/layout.h`.
2. **Fader direction.** If a fader reads backwards, set its entry in `kInvert` in
   the same file.
3. **CPU.** The debug log prints average and peak load once a second. The worst
   case (everything up, ten pads) is 80x real time on an M-series Mac, which
   should be comfortable at 480 MHz but has not been measured on the Seed.

## Layout

```
src/dsp/    wind, rain, thunder, ocean, drone, reverb, engine (portable, header-only)
src/ui/     panel -> engine: knobs, switches, pad latching, ghost notes
src/hw/     the Touch 2 board layer (ADC, switches, MPR121 pads), LED
tests/      host tests and the WAV renderer
```

## Credits

The board layer (`src/hw/`) is adapted from my SpotykachTouch port, which takes its
pin map and MPR121 settings from Synthux Academy's TouchString firmware (MIT).
The reverb is a from-scratch feedback delay network.
