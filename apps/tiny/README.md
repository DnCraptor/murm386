# TINY 2.1 for murm386 (native ARM/EZ)

Port of the user-supplied Turbo C 2.0 `TINY.C` with the original trained
`TINY.BIN` network. `tiny.c` retains the neural model and original Turbo C
full-screen UI; `tiny_native.c` provides the video, keyboard, timer and PC
speaker adapters through murm386's existing native DOS API.

## Build

From the murm386 repository root (RP2350 SDK/toolchain and host `elf2ez`
installed):

```sh
cmake -S apps/tiny -B apps/tiny/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build apps/tiny/build
```

The post-build step writes `apps/compiled/tiny.exe` (native EZ, not an x86
MZ executable) and copies `TINY.BIN` to `apps/compiled`. The `.BIN` is a **data
file**, not an executable. Place `tiny.exe` and `TINY.BIN` in the same DOS
working directory on the SD card and launch `TINY` from that directory.

## Usage

The original UI is enabled by default: F1 help; F2 sound; F3 voice; F4
creativity; Up repeats the last question; Esc exits. `TINY 0` selects
non-random decoding; `TINY 9` maximizes variability. `TINY /P` selects plain
console mode, suitable for redirected stdin/stdout. Also supports `/DEMO`,
`/QUIET`, `/MONO`, `/CLASSIC`.

## Platform notes

- The archive's model contains 4 layers of width 96, with intermediate width
  256. The original implementation allocates roughly 0.5 MiB for weights and
  attention history; the native malloc prefers PSRAM.
- 80x25 BIOS text mode, native text backing store, and keyboard via existing
  DOS BIOS services. PC speaker uses PIT channel 2 and port 0x61.
- FP arithmetic and `sqrt`/`exp` use `apps/api/native-dos-runtime.cmake`.
  Performance on RP2350 has **not** been measured.
- The original Turbo C screen dump/debug switches have not been validated
  on hardware. Do not claim that this port has been device-tested.
- No modifications to the murm386 firmware/runtime API are required.
- The source archive does not include explicit redistribution license terms;
  confirm permission before publishing outside your own builds.
