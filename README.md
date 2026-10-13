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
| P2 (A1) | **Wind**: level, pitch and how gusty |
| P3 (A2) | **Rain**: from a few drops a second to a downpour |
| P4 (A3) | **Thunder**: from a distant bed of rumble to frequent, close strikes. The LED flashes with the lightning |
| P5 (A4) | **Ocean**: two layers of surf on unrelated clocks, brightening as each wave breaks |
| P1 (A0) | **Drone**: level, and with it brightness |
| P6 (A5) | **Motion**: how far the weather wanders on its own (0 = exactly as set) |
| S1, left slider (A6) | **Reverb**: wet amount and tail length (1.5 s up to ~12 s) |
| S2, right slider (A7) | **Volume** |
| Switch A | scale: **up** major pentatonic (bright), **centre** minor pentatonic (dark), **down** suspended |
| Switch B | register: **up** +1 octave, **centre** mid, **down** -1 octave |
| Pads P00-P09 | latch drone notes (tap to add, tap again to fade). Two octaves of the scale, from the root |
| P10 / P11 (the two small pads at the top) | root **down / up** a semitone |
| P10 + P11 held together (~0.6 s) | **release all** notes |

It starts on D with a root and fifth already sounding. Held notes glide to a new
root, scale or register rather than jumping. Five-note scales only, so any
combination of pads sounds like a chord.

The Seed's LED shows lightning, a faint glow that swells with the surf, and a
blink for every pad.

Knobs are named by the faceplate: P2-P5 across the top are the four weather
knobs, P1 (below P2) is drone and P6 (below P5) is motion; S1 and S2 are the
sliders. All checked against the board with the DEBUG log.

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

1. **Slider direction.** S1 reads 0 and S2 reads 1000 untouched; if a slider runs
   backwards, set its entry in `kInvert` in `src/ui/layout.h`.
2. **CPU.** `make DEBUG=1` logs load once a second (about 30% with two notes and
   mid-position knobs). Worst case is not measured on the board.

`make DEBUG=1` also logs every pad as it is touched, the panel, and every digital
header pin that changes, which is how the layout above was confirmed.

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
