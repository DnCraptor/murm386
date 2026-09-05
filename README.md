# murm286/386 RP2350 "PC" firmware

RP2350-based PC/AT-compatible firmware with a 286-class x86 core, native FreeDOS kernel integration, VGA/HDMI output, SD-card storage, PS/2/USB input and multiple audio backends.

The current production target is **286**. A 386 core still exists in the tree, but it has not been regression-tested for a long time and is intentionally excluded from the normal build scripts.

## Current status

The firmware is actively developed around the RP2350 and currently targets these board profiles:

- **M1** — Murmulator/FRANK M1 pinout
- **M2** — Murmulator/FRANK M2 pinout
- **PC** — Olimex PICO-PC profile
- **Z2** — Waveshare RP2350-PiZero profile
- **C2** — FRANK Core 2 profile

The authoritative GPIO map is `src/board_config.h`. Board-specific Pico SDK headers are in `boards/`.

## CPU and DOS

- Production CPU target: **286** (`CPU_TARGET=286`).
- The emulator core also contains 386 support, but this branch is currently considered experimental/untested.
- The DOS environment uses the RP2350 port of the FreeDOS kernel in `src/fdos/`.
- The native command shell is FCOM (`src/fdos/fcom/`).
- BIOS and DOS services are integrated into the firmware; the project is no longer the old “SeaBIOS + external VGA BIOS” layout described by earlier versions of this README.

## Video profiles

The normal firmware is built as `VIDEO_MODE=RUNTIME`. Win+F11 selects the guest-visible adapter profile and stores it in `.config/286/config.ini`; the change takes effect after restart.

| Runtime profile | Guest video RAM | Notes |
|---|---:|---|
| `MCGA` | 64 KiB | MCGA capabilities |
| `EGA128` | 128 KiB | EGA, 128 KiB |
| `EGA256` | 256 KiB | EGA capabilities with the 256 KiB physical layout |
| `VGA128` | 128 KiB | VGA, 128 KiB |
| `VGA256` | 256 KiB | VGA/VBE 1.2, 256 KiB |

Physical output is VGA or HDMI depending on the board and runtime/forced output selection. `--vga` and `--hdmi` in the build scripts force one path where the board supports it.

### Guest RAM backends

The production build has two memory-model classes:

- `RUNTIME`, `NO_PAGING=OFF` — normal paging firmware containing all five runtime video profiles;
- `VGA256`, `NO_PAGING=ON` — separate direct-QSPI guest-RAM firmware, named with the `-np` suffix.

For runtime profiles with 64/128 KiB VRAM, the unused tail of the 256 KiB `gfx_buffer` is used as the guest paging cache: MCGA gets 192 KiB / 96 pages, while EGA128 and VGA128 get 128 KiB / 64 pages. EGA256 and VGA256 use the dedicated 40 KiB `RAM_4_EXT` cache (20 pages). The selected `video=` profile is loaded before this memory layout is configured.

The paging implementation lives in `src/ega128_paging.c`. SD-backed paging uses `tmp/pagefile.sys`; supported hardware can use its PSRAM paging backend when available.

## EMM (for -emm.uf2 only)

EMM (Expanded Memory Manager) is the software driver used to manage EMS (Expanded Memory Specification) in DOS.

The LTEMM driver in `config.sys` must be loaded with `/P:D000` for the `-emm` firmware variant. Example:
```text
DEVICEHIGH=C:\LTEMM.EXE /P:D000
```

## Audio

Supported firmware audio backends are:

- **I2S**
- **PWM**

The emulated PC audio devices include PC speaker, AdLib/OPL, Sound Blaster-compatible paths, Tandy, Covox and Disney Sound Source support.

Backend availability follows the board pinout at runtime: M1/M2/Z2 can select PWM or I2S, `PC` is PWM-only, and `C2` is I2S-only.

## Input

Depending on board capabilities:

- PS/2 keyboard and mouse;
- USB HID host keyboard/mouse;
- NES/SNES-style gamepad support.

USB host/device role is a runtime configuration; there is no longer a build-time `USB_HID_ENABLED` switch in the normal build scripts.

## SD card layout

Production builds use the CPU target as their data directory. For the currently supported 286 build this is:

```text
SD root/
└── .config/
    └── 286/config.ini
└── 286/
    └── disk images ...
└── tmp/
    └── pagefile.sys      # created/used by SD paging when needed
```

`config.ini` is optional; the firmware can start with defaults and the runtime settings/disk manager can create or update configuration.

## Building

The recommended entry points are:

- Linux/macOS/WSL: `./build.sh`
- Windows: `build.bat`
- the complete supported 286 production matrix: `build_all.sh` / `build_all.bat`

Examples:

```sh
./build.sh -M1 -RUNTIME -504 -p 66 --clean
./build.sh -M2 -RUNTIME --hdmi
./build.sh -M2 -VGA256 --no-paging
./build_all.sh 286
```

The single-build scripts always pass `CPU_TARGET=286` intentionally. The all-build scripts accept `286` as an explicit CPU-target argument for forward compatibility, but currently reject `386` because that branch is not considered tested. `build_all` builds both `EMM=OFF` and `EMM=ON` variants. For each board/EMM combination it builds one `RUNTIME` paging firmware and one `VGA256 --no-paging` firmware: **20 builds** total. Audio output is runtime-selected and does not multiply the build matrix.

The single-build wrappers default to `RUNTIME` with paging enabled. `--no-paging` is reserved for the separate `VGA256` direct-QSPI memory model.

See [README-host-build.md](README-host-build.md) for toolchain setup, script options, build matrix and output locations.

## Output names

CMake encodes the important build parameters in the firmware name. Typical output looks like:

```text
m1p2-286-RUNTIME-504MHz-1.6V-P66-v1.16.uf2
```

Outputs are written under:

```text
bin/<CMAKE_BUILD_TYPE>/
```

## Runtime controls

At power-on, the physical video output can be overridden before video autodetection starts. Hold **V** on a PS/2 or USB keyboard, or **SELECT+B** on the NES/SNES-style gamepad, to force VGA (`SELECT_VGA=true`). Hold **H**, or **SELECT+A**, to force HDMI (`SELECT_VGA=false`). When either override is detected, normal VGA/HDMI pin autodetection is skipped for that boot. Compile-time `--vga` / `--hdmi` forced builds keep their explicit build-time selection.

The **VGA/HDMI** item at the bottom of the Win+F11 Hardware setup menu controls the persistent boot selection. **Autodetect** is the default and removes both marker files. **VGA** creates `/.config/286/force_vga`; **HDMI** creates `/.config/286/force_dvi`. Selecting either forced output removes the opposite marker. The marker files are updated immediately when the value is changed; no Apply or restart is requested by this setting itself. The selected physical output is used on the next boot.


### Physical audio output

The physical audio backend is selected at runtime; separate I2S and PWM firmware builds are no longer required. On boards that provide both backends, **Autodetect** is the default and probes the audio pins at startup. The Win+F11 **Audio output** item selects `Autodetect / PWM / I2S` using the same early sticky-marker mechanism as video: **PWM** creates `/.config/286/force_pwm`, **I2S** creates `/.config/286/force_i2s`, and **Autodetect** removes both files. Selecting either forced output removes the opposite marker. Marker files are updated immediately; no Apply or restart is requested by this setting itself. The selected physical backend is used on the next boot. PC hardware is PWM-only and C2 is I2S-only, so those boards show their fixed backend.

For automatic detection on dual-backend boards the firmware first drives the probe pins low for 50 ms to discharge residual RC-filter charge, then uses the same `testPins()` electrical-link test used by video detection. A probe that still reads HIGH under pull-down is treated as external back-feed and falls back to PWM. The selected backend is initialized after `config.ini` has been loaded.
The project contains the on-screen settings and disk-management UI. Current key bindings are implemented in `src/main.c`; consult that source when changing input mappings so documentation does not drift from code again.

```text
Win+F10 - Toggle USB HOST/DEVICE mode and reboot
Win+F11 - Hardware setup
Win+F12 - Attached disks / USB-device setup
Ctrl+Alt+Del - soft reset
```


## Source layout

```text
src/                 emulator, BIOS, platform and DOS integration
src/fdos/           native FreeDOS kernel port and FCOM
apps/                native applications and API examples
drivers/             video, audio, SD, PSRAM, PS/2, USB HID, gamepad
boards/              RP2350 board definitions used by Pico SDK
slave/               FRANK Core 2 slave firmware sources
```

## Upstream

The x86 emulator is derived from Tiny386 by Chunhui He and has since accumulated substantial RP2350-specific CPU, memory, BIOS, DOS, video and peripheral work.
