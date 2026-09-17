# murm386 1.18.0 release notes

Version 1.18.0 is a broad hardware, storage, input, audio and CPU-core update. The 386 core is buildable again and is included in the Windows release build matrix alongside the 286 core.

## Highlights

- Restored the 386 build path and added the real `CPU_TARGET=386` / `I386_MODE=1` configuration to the supported Windows build scripts.
- Added USB modem support for ESP32 ZiModem, including 8250 UART integration, reset handling and in-firmware ESP modem firmware flashing.
- Added USB mass-storage disk support, including GPT-to-MBR partition mapping for the emulated PC disk interface.
- Added configurable SD-card HDD placement: `Off`, `First` or `Last`.
- Added hardware AY output support for AY-3-8910 and AY8930.
- Added Covox Sound Master emulation, including AYDMA/DAC operation, 220h/240h base options and DMA channel selection/fixes.
- Expanded joystick support: multiple USB joysticks, improved axis handling, two NES pads, mouse-to-joystick mode and button swapping.
- Added Win+F10 USB HOST/DEVICE mode switching.
- Updated the project to Pico SDK 2.3.1.

## Storage and BIOS

- Fixed AT-class floppy detection/reporting.
- Made explicitly configured CHS geometry authoritative for emulated disks.
- Added additional disk caching and related DOS/DOOM fixes.
- Added USB-drive handling and GPT partition mapping.
- Added SD-card First/Last ordering support.

## Input and DOS integration

- Fixed INT 33h mouse handling and tuned mouse speed.
- Improved USB mouse handling and broadened USB joystick compatibility.
- Added multiple USB joystick support, joystick-axis fixes, two NES pads, mouse-as-joystick mode and configurable joystick button swapping.
- Fixed Alt-key handling.
- Fixed FCOM CR/LF handling and `%var%` expansion issues.
- Fixed the `prn.txt`/BOUND-name collision; printer output uses `printed.txt`.

## Audio

- Added hardware AY-3-8910 output and follow-up mode fixes.
- Added hardware AY8930 output.
- Added Covox Sound Master support with AYDMA/DAC, 220h and 240h base-address variants and DMA1/DMA3 selection.
- Fixed Sound Master DMA/IRQ behaviour and AYDMA operation.

## Platform and performance

- Fixed RP2350-PiZero/Z2 startup issues.
- Added support for higher RP2350 voltage settings up to 1.8 V where configured by the platform code.
- Added i386 performance optimizations and build/linker adjustments.
- Updated Pico SDK integration to 2.3.1.

## Build matrix

`build.bat` accepts `--cpu 286|386` (also `-286` / `-386`). `build_all.bat` now builds both CPU targets across the five supported boards, both EMM states and the `RUNTIME` plus `VGA256 --no-paging` firmware classes: 40 Windows release variants in total.
