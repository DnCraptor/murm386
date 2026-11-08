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
| `core/` | handle table, KERNEL subset, regions, 1/8 bpp raster engine, GDI, fonts, software cursor |
| `user/` | windows, messages, painting, non-client area, controls (BUTTON, STATIC, EDIT, LISTBOX), dialogs, MessageBox |
| `drivers/dos/` | `VBE8` display driver (VBE 100h 640x400x8, fallback 13h) and `DOS` input driver (native keyboard, INT 33h mouse) |
| `runtime/` | EZ `main()` that brings everything up and calls `WinMain` |
| `samples/` | applications; `hello` is a tour of the API |
| `host/` | the same sources on Linux with a snapshot display and scripted input |

## Drivers

The core never touches video hardware. A display driver reports a
framebuffer as a surface (pointer, size, pitch, bpp), accepts palette
updates and is told which rectangles changed. Another mode or another
device is another driver; the core works at any size. 101h (640x480x8) does
not fit the 256 KB video memory, so the default is 100h (640x400x8).
Drivers are tables of functions registered by name, ready for loading as
separate modules later.

## Building

Device (EZ application, written to `apps/compiled/<app>.exe`):

    cmake -S apps/win -B build-win -DWIN_APP=hello
    cmake --build build-win

Host (needs `gcc-multilib`, the core is built 32-bit like the target):

    cd apps/win/host && make
    ./hello scripts/hello_tour.txt     # writes tour_*.ppm snapshots
    ./test_gdi                         # GDI sample sheet, gdi.ppm

## Keyboard

Ctrl+Shift switches between the US and Russian (Windows-1251) layouts.
Text is ANSI Windows-1251 and is drawn with the CP866 VGA font.

## Not yet

Menus, scroll bar and combo box controls, resources (stage 2 brings the NE
loader), clipboard, printing, MDI.
