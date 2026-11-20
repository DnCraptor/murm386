# VGA320.DRV — 320x240x256 for native WINN

Separate native Windows display driver. No change to WINN.EXE, the BIOS, or
murm386 firmware is required. The GDI screen really is 320x240, 8-bit indexed
(256 palette entries); applications receive a 320x240 windowing coordinate
system, not a 640x400 framebuffer disguised by window scaling.

The emulator BIOS does **not** implement a hardware 320x240 8-bpp mode. For
320x240, this driver uses existing VBE 100h (640x400x256) and scales changed
rectangles from an allocated 320x240 shadow surface to a centred 533x400
viewport. The approximate 5:3 scaling in both directions preserves the 4:3
aspect ratio on the physical 640x480 HDMI output. The unused 53/54-pixel side
borders remain black. This is not a new BIOS/VBE mode.

An alternative true VGA mode 13h, **320x200x256**, is also supported; in that
mode GDI draws directly into the physical framebuffer with no conversion.
This is the actual DOS Doom graphics mode. It is useful as an A/B comparison
of audio glitches versus the 320x240 virtual display.

## Configuration

Copy `apps/compiled/VGA320.DRV` beside `WINN.EXE`, and set `SYSTEM.INI`:

```ini
[boot]
display.drv=VGA320.DRV

[display]
width=320
height=240
```

For true VGA mode 13h, change `height=200`. The default with no explicit size
hints is 320x240. A different resolution or colour depth is rejected.

## Build

From the repository root (Pico SDK 2.2.0, Arm GCC, elf2ez):

```sh
cmake -S apps/windrv/vga320 -B build-vga320 -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-vga320
```

The output is `apps/compiled/VGA320.DRV`. The existing
`apps/win/build_win.bat` / `.sh` automatically finds this project, like the
other `apps/windrv/*` modules. The build reuses the known-good SDK importer,
linker script and unresolved-symbol check from `apps/windrv/vga800/`.

Host-only regression test (no Pico toolchain):

```sh
cc -std=c11 -Wall -Wextra -Werror -DVGA320_HOST_TEST \
   -Iapps/win/ddi apps/windrv/vga320/vga320.c \
   apps/windrv/vga320/test_vga320.c -o /tmp/test_vga320
/tmp/test_vga320
```

## Notes

- For 320x240, `flush()` copies only modified GDI rectangles. The physical
  mode stays VBE 100h, so its firmware render path is VBE8, not EGA16.
- The shadow screen occupies 76,800 bytes, and the physical VBE100 framebuffer
  256,000 bytes (already allocated by the emulator).
- The existing PIO HDMI limitation on palette indices 252–255 in VBE 100h
  also applies here (no firmware modification is made).
- Applications with hard-coded minimum window sizes may not fit 320x240.
- The driver never changes audio devices, PWM, HDMI clocks, or IRQ priorities.
