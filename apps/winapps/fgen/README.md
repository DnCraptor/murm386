# FGEN.EXE - native function generator for WINN

Independent native two-channel function generator for `murm386` WINN,
functionally modeled on the controls of the historical Marchand Function
Generator (`zvukogener.exe` / `FG.exe`). No original executable code is
included.

## Features

- One common frequency, entered as an integer 0-20000 Hz or adjusted by slider.
- Each channel independently selects Sine, Square, Sawtooth, or Pulse.
- Separate amplitude 0-100% for left/right.
- 44100 Hz, signed 16-bit, interleaved stereo PCM.
- Streaming native ARM oscillator: incremental phase, 256-entry sine lookup,
  4096-byte blocks. There are no per-sample divisions or x86 instructions.
- Direct access to the installed native wave-driver interface
  (`SYSTEM.INI` `[drivers] wave=SBWAVE.DLL`). Wave and Master volumes
  follow the installed `MMSYSTEM.DLL`; the app does not overwrite them. The mixer mute
  switches are not yet reflected in this direct-driver prototype.

The source is a fresh implementation, not a sample-for-sample port of the
original proprietary program. In particular, the pulse waveform is 10% duty,
and sine interpolation/digital aliasing differ from the old EXE.

## Build

As for the existing native Clock app:

```text
cmake -S apps/winapps/fgen -B apps/winapps/fgen/build
cmake --build apps/winapps/fgen/build
```

The WINN SDK emits `apps/compiled/FGEN.exe`. Place it alongside WINN.EXE,
MMSYSTEM.DLL and SBWAVE.DLL, or where Program Manager launches native apps.
`SYSTEM.INI` must have a supported `wave` driver in `[drivers]` and SB16
emulation enabled. The wave device is exclusive: FGEN cannot play if already
opened by another application.

## Host DSP test

```sh
cc -std=c11 -Wall -Wextra -Werror -O2 \
    apps/winapps/fgen/synth.c apps/winapps/fgen/test_synth.c \
    -o /tmp/fgen-test
/tmp/fgen-test
```

## Known limits

- Playback and GUI handling are tied to WINN's 10-ms window timer, but no
  extensive guest x86 computations occur between ticks.
- The application uses the native `win_wave_driver` interface directly, not
  Win16 `waveOutOpen`, so normal MMSYSTEM device ownership/exclusion remains
  enforced by the driver, not the higher-level waveOut queue.
- An underlying `SBWAVE` DMA/IRQ glitch will still be audible; this app is
  intended to separate wave-driver limitations from x86 CPU emulation costs.
- Frequency slider is coarse at low values; use the edit field for 1-Hz steps.
