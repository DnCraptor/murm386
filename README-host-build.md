# Host build guide

This document describes the current supported host-side build workflow for murm386/FRANK firmware.

## Supported production target

The normal build scripts deliberately build:

```text
CPU_TARGET=286
```

The 386 core remains in the source tree, but it has not been regression-tested recently. `build.sh` and `build.bat` therefore do not expose a 386 switch. `build_all.sh` / `build_all.bat` accept an optional CPU-target argument only for interface stability and currently accept **286 only**.

## Toolchain

The project CMake file is set up for Pico SDK 2.2.0 and the RP2350 ARM toolchain used by the Pico VS Code extension:

- Pico SDK: **2.2.0**
- ARM GCC toolchain: **14_2_Rel1**
- picotool: **2.2.0**
- CMake: **3.13 or newer**

Other compatible Pico SDK/toolchain installations may work, but these are the versions encoded in the current tree.

### Linux / macOS / WSL

Either use a Pico SDK installation discoverable by the project, or export `PICO_SDK_PATH` before configuring:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
```

Ninja must be available. The build scripts explicitly configure CMake with `-G Ninja` so the same generator/layout is used on every host.

### Windows

The easiest supported setup is the Raspberry Pi Pico VS Code extension. The project automatically includes:

```text
%USERPROFILE%/.pico-sdk/cmake/pico-vscode.cmake
```

when that file exists.

`build.bat` requires `cmake`, `ninja`, and the ARM toolchain to be available in the environment. Running it from a Pico SDK-enabled terminal or the VS Code integrated terminal is recommended. If an existing build directory was created by Visual Studio/MSBuild, the script detects the generator mismatch and recreates that build directory automatically.

## Single-variant build

Linux/macOS/WSL:

```sh
./build.sh [options]
```

Windows:

```bat
build.bat [options]
```

Defaults:

| Setting | Default |
|---|---|
| CPU target | `286` (fixed) |
| Board | `M1` |
| Video | `RUNTIME` |
| Audio | `I2S` |
| RP2350 clock | `504` MHz |
| PSRAM max clock | `66` MHz |
| Build type | `Release` |
| Build directory | `build` |
| Clean configure | enabled (`CLEAN=1`) |

### Main options

| Option | Values / meaning |
|---|---|
| `-b`, `--board` | `M1`, `M2`, `PC`, `Z2`, `C2` |
| `-v`, `--video` | `RUNTIME`, `MCGA`, `EGA128`, `VGA128`, `VGA256` |
| `-a`, `--audio` | `I2S`, `PWM` |
| `-c`, `--clock` | RP2350 clock in MHz |
| `-p`, `--psram` | maximum QSPI PSRAM clock in MHz |
| `--vga` | force VGA output |
| `--hdmi` | force HDMI output |
| `--debug` | `DEBUG_ENABLED=ON` |
| `--diag` | `DIAG_ENABLED=ON` |
| `--emm` | `EMM=ON` |
| `--no-paging` | disable guest-RAM paging; valid with `VGA256` |
| `--build-type` | CMake build type, default `Release` |
| `--build-dir` | alternate CMake build directory |
| `-j`, `--jobs` | explicit parallel-job count |
| `--clean` | delete the selected build directory first |

Short forms are also accepted:

```text
-M1 -M2 -PC -Z2 -C2
-RUNTIME -MCGA -EGA128 -VGA128 -VGA256
-i2s -pwm
-252 -378 -504
```

Examples:

```sh
./build.sh -M1 -VGA256 -i2s -504 -p 66 --clean
./build.sh -M1 -EGA128 -i2s -504 -p 66
./build.sh -M2 -VGA128 -pwm --hdmi
./build.sh -Z2 -VGA128 -i2s
```

Windows uses the same options:

```bat
build.bat -M1 -VGA256 -i2s -504 -p 66 --clean
```

### Board-specific audio rules

The scripts mirror CMake hardware constraints:

- `PC`: PWM is forced;
- `C2`: I2S is forced;
- `M1`, `M2`, `Z2`: both I2S and PWM variants are buildable.

C2 also has board-specific video/input constraints in `CMakeLists.txt`; CMake remains the final authority.

## Building all supported variants

Linux/macOS/WSL:

```sh
./build_all.sh 286
```

Windows:

```bat
build_all.bat 286
```

The CPU argument is optional and defaults to `286`. Passing `386` currently stops with an error instead of silently producing an untested release matrix.

Additional options are forwarded to every `build.sh` / `build.bat` invocation. `--emm` is intentionally rejected here because `build_all` controls EMM itself and builds both states. For example:

```sh
./build_all.sh 286 -504 -p 66 --clean
```

The production matrix now has two firmware classes for each valid board/audio/EMM combination:

1. `RUNTIME` — paging firmware. Win+F11 selects `MCGA 64K`, `EGA 128K`, `EGA 256K`, `VGA 128K` or `VGA 256K`; the selected profile is stored as `video=` in `286/config.ini` and takes effect after restart.
2. `VGA256 --no-paging` — separate direct-QSPI guest-RAM firmware (`-np`). `RUNTIME` cannot be combined with `--no-paging`.

There are 16 valid board/audio/EMM combinations per firmware class (M1/M2/Z2: I2S or PWM; PC: PWM; C2: I2S), so the complete matrix contains **32 builds**.

Each variant gets a separate directory below:

```text
build/all/<board>-286-<video>-<audio>/
build/all/<board>-286-<video>-<audio>-emm/
```

This prevents stale CMake cache values from one variant leaking into another.

## Firmware output

CMake writes firmware to:

```text
bin/<build-type>/
```

For example:

```text
bin/Release/m1p2-286-VGA128-504MHz-1.6V-P66-I2S-v1.14-np.uf2
```

The output name is generated by CMake from board, CPU target, video profile, clock, PSRAM limit, audio type and firmware version.

## Video/RAM implications

`RUNTIME` is the normal paging build. The active adapter is selected at runtime with Win+F11 and persisted in `[frank-386]` as `video=MCGA`, `EGA128`, `EGA256`, `VGA128` or `VGA256`.

| Runtime profile | VRAM | Paging cache placement |
|---|---:|---|
| `MCGA` | 64 KiB | `gfx_buffer + 64K`, 192 KiB / 96 pages |
| `EGA128` | 128 KiB | `gfx_buffer + 128K`, 128 KiB / 64 pages |
| `EGA256` | 256 KiB | `RAM_4_EXT`, 40 KiB / 20 pages |
| `VGA128` | 128 KiB | `gfx_buffer + 128K`, 128 KiB / 64 pages |
| `VGA256` | 256 KiB | `RAM_4_EXT`, 40 KiB / 20 pages |

`EGA256` uses the same 256 KiB physical SRAM layout as `VGA256`, but exposes EGA BIOS/INT 10h/BDA capabilities rather than VGA/VBE capabilities.

The profile must be loaded from `config.ini` before the paging layout is configured. The paging implementation lives in `src/ega128_paging.c`; SD-backed paging uses `286/pagefile.sys`, and supported boards can use their PSRAM paging backend when available.

`NO_PAGING=ON` selects the separate direct-QSPI guest-RAM memory model and is intentionally restricted to `VGA256`. The wrapper scripts expose this as `--no-paging`; CMake appends `-np` to the firmware basename. Normal `RUNTIME` builds explicitly use `NO_PAGING=OFF`.

## CMake without wrapper scripts

The equivalent direct configure command is:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPU_TARGET=286 \
  -DBOARD=M1 \
  -DVIDEO_MODE=RUNTIME \
  -DAUDIO_TYPE=I2S \
  -DCPU_SPEED=504 \
  -DPSRAM_SPEED=66 \
  -DNO_PAGING=OFF
cmake --build build --parallel
```

On Windows the same `-D` options are used; only shell quoting differs.

## Important CMake options not exposed by the normal scripts

`NO_PAGING` still defaults to `ON` at the raw CMake level, but the normal wrapper scripts pass `NO_PAGING=OFF`. Use `--no-paging` only for the separate `VGA256` direct-QSPI build.

The project also contains developer/profiling switches such as `PROFILE_ENABLED`, `SUBSYS_PROFILE`, `PIN_CLOCKS`, `CODE_PROFILE`, `PC_SAMPLE`, `BB_PROFILE`, `FAST_FETCH`, `DISK_CACHE`, `AUTOTYPE` and `CONTROL_STACK`.

They are intentionally not part of the normal build-script interface. Use direct CMake options for development experiments so production build scripts stay deterministic.

## SD data directory

The data directory is tied to `CPU_TARGET` by CMake. Current supported builds use:

```text
286/
```

This directory contains `config.ini`, disk images and (when SD-backed paging is active) `pagefile.sys`.

## Clean/reconfigure behaviour

`build.sh` / `build.bat` rerun CMake configure with the normal wrapper options. In the current Windows `build.bat`, `CLEAN=1` is the default, so the selected build directory is removed before every build; `--clean` therefore does not change the default Windows behaviour.

`build_all` additionally uses one build directory per board/video/audio/EMM variant. This is intentional: several project options are CMake cache entries and must not accidentally carry state from another board/video/audio build.

## Release script

`release.sh` is a separate legacy release-packaging workflow and is not used by `build_all.sh` / `build_all.bat`. When release policy is updated, it should be reviewed independently against the current 286-only production matrix rather than assumed to match these scripts automatically.
