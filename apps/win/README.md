# Native Windows 3.x API for murm386

An MIT-licensed implementation of the Windows 3.x API (KERNEL, GDI, USER
subset) for native EZ applications. Stage 1 runs native ARM programs written
against `windows.h`; stage 2 will load Win16 NE executables on the 386 core
and route their KERNEL/GDI/USER imports to the same code.

## Layout

| Path | Contents |
|------|----------|
| `include/` | `windows.h`, `winuser.h`: the API applications see |
| `ddi/winddi.h` | driver interface: display (DIB surface + palette) and input |
| `ddi/winmod.h` | loadable driver modules: entry point and the tables exchanged with the core |
| `core/` | handle table, KERNEL subset (memory, strings, atoms, INI files), regions, 1/8 bpp raster engine, GDI, fonts, software cursor, module loader |
| `user/` | windows, messages, painting, non-client area, menus and accelerators, controls (BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR), dialogs, MessageBox |
| `drivers/dos/` | `VBE8` display driver (VBE 100h 640x400x8, fallback 13h) and `DOS` input driver (keyboard through the BIOS buffer, INT 33h mouse) |
| `runtime/` | EZ `main()` that brings everything up and calls `WinMain` |
| `../windrv/<name>/` | driver modules; `vga800` is VBE 102h 800x600 in 16 colours |
| `samples/` | applications; `hello` is a tour of the API |
| `host/` | the same sources on Linux with a snapshot display and scripted input |

## Drivers

The core never touches video hardware. A display driver reports a
framebuffer as a surface (pointer, size, pitch, bpp), accepts palette
updates and is told which rectangles changed. Another mode or another
device is another driver; the core works at any size. 101h (640x480x8) does
not fit the 256 KB video memory, so the built-in default is 100h
(640x400x8).

Drivers can be separate files. A module is an EZ image built without crt0
whose `__ez_start` takes the core's service table and returns its driver
tables; the core reads it, relocates it and calls it itself, no firmware
support involved. `SYSTEM.INI` next to `WINN.EXE` names the modules:

    [boot]
    display.drv=VGA800.DRV

Without the file, or if a module fails to load, the built-in drivers are
used. `VGA800.DRV` keeps an 8 bpp copy of the screen and converts changed
rectangles to the four VGA planes, mapping colours to the standard 16.

## Building

Device (written to `apps/compiled/winn.exe`; `-DWIN_APP=<sample>` picks the
application, default `hello`):

    cmake -S apps/win -B build-win
    cmake --build build-win

Driver module (written to `apps/compiled/VGA800.DRV`):

    cmake -S apps/windrv/vga800 -B build-vga800
    cmake --build build-vga800

Host (needs `gcc-multilib`, the core is built 32-bit like the target):

    cd apps/win/host && make
    ./hello scripts/hello_tour.txt     # writes tour_*.ppm snapshots
    ./hello scripts/menus.txt          # menus, combo box, scroll bar
    ./hello800 scripts/vga800.txt      # the VGA800 driver, built in
    ./test_gdi                         # GDI sample sheet, gdi.ppm

## Keyboard

Ctrl+Shift switches between the US and Russian (Windows-1251) layouts.
Text is ANSI Windows-1251 and is drawn with the CP866 VGA font.

## Not yet

Resources (stage 2 brings the NE loader), clipboard, printing, MDI.
