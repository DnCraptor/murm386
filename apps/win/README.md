# Native Windows 3.x API for murm386

An MIT-licensed implementation of the Windows 3.x API (KERNEL, GDI, USER
subset) for native ARM programs. `WINN.EXE` is the Windows loader: it holds
KERNEL, GDI, USER and the built-in drivers, loads driver modules, and runs
applications (separate EZ files) as cooperative tasks, as Windows 3.x does.
Stage 2 will load Win16 NE executables on the 386 core and route their
imports to the same export tables.

    WINN [program [arguments]]

`SYSTEM.INI` next to `WINN.EXE` (see the sample here) names the shell
(`[boot] shell=`), started first; Windows ends when it ends. Without the key
the shell is `PROGMAN.EXE`, if it is there, as in Windows 3.x; `shell=` with
nothing leaves the program from the command line in that part (`WINN DEMO`).

## Layout

| Path | Contents |
|------|----------|
| `include/` | `windows.h`, `winuser.h`, `commdlg.h`: the API applications see |
| `ddi/winddi.h` | driver interface: display (DIB surface + palette) and input |
| `ddi/winmod.h` | loadable driver modules: entry point and the tables exchanged with the core |
| `core/` | handle table, KERNEL subset (memory, strings, atoms, INI files, files, tasks), regions, 1/8 bpp raster engine, GDI, fonts, software cursor, module loader, export tables |
| `user/` | windows, messages, painting, non-client area, menus and accelerators, controls (BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR), dialogs, MessageBox, MDI (MDICLIENT, DefFrameProc, DefMDIChildProc), clipboard, common dialogs (module COMMDLG: Open, Save As, Find, Replace) |
| `drivers/dos/` | `VBE8` display driver (VBE 100h 640x400x8, fallback 13h) and `DOS` input driver (keyboard through the BIOS buffer, INT 33h mouse) |
| `runtime/` | `WINN.EXE` `main()`: drivers, core, shell and command-line program as tasks |
| `sdk/` | for applications: API stubs by Win16 ordinal, `__ez_start`, `winapp.cmake`; `gen_exports.py` makes the export tables |
| `../winapps/<name>/` | applications: `progman` (the Program Manager, the default shell), `notepad`, `demo` (a tour of the API) |
| `../windrv/<name>/` | driver modules; `vga800` is VBE 102h 800x600 in 16 colours |
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

## Applications

An application is an EZ image built with `sdk/winapp.cmake`: no crt0, its
entry `__ez_start` (from `sdk/winapp_entry.c`) gets the export tables from
`WINN.EXE` and calls `WinMain` on the application's own task stack. Each
API function the program uses is a small stub that jumps through
`table[module][ordinal]`, with the Win16 ordinals of KERNEL, GDI and USER
(native extensions such as `DialogBoxIndirectN` are module `WINN`). Started
from DOS directly, an application only prints that it needs Windows.
`__ez_start` begins with a branch over a signature word; `WinExec` refuses
EZ files without it (DOS programs, drivers) with error 11, and `IsWinAppN`
checks a file the same way without loading it.

Tasks switch only in `GetMessage`, `PeekMessage`, `Yield` and `WinExec`.
Every instance has its own copy of the program, so window classes belong to
the instance that registered them.

After adding an API function, run `sdk/gen_exports.py` (it updates
`core/exports.c` and `sdk/winapi_stubs.S`, both committed).

## Building

Everything at once, each project in a fresh build directory under
`build/win`, into `apps/compiled`:

    apps/win/build_win.sh            # or apps\win\build_win.bat

Identical sources give identical files only with the same compiler, Pico SDK
and build type. The scripts fix the build type (MinSizeRel) and take the SDK
2.2.0 and the Arm GNU Toolchain 14_2_Rel1 of the Pico VS Code extension (or
`PICO_SDK_PATH` / `PICO_TOOLCHAIN_PATH`); they end with the compiler, the SDK
and a SHA-256 of every file, so two builds can be compared. One project:

    cmake -S apps/win -B build-win                 # apps/compiled/winn.exe
    cmake --build build-win
    cmake -S apps/windrv/vga800 -B build-vga800    # apps/compiled/VGA800.DRV
    cmake --build build-vga800
    cmake -S apps/winapps/progman -B build-pm      # apps/compiled/PROGMAN.exe
    cmake --build build-pm
    cmake -S apps/winapps/notepad -B build-np      # apps/compiled/NOTEPAD.exe
    cmake --build build-np
    cmake -S apps/winapps/demo -B build-demo       # apps/compiled/DEMO.exe
    cmake --build build-demo

Host (needs `gcc-multilib`, the core is built 32-bit like the target; tasks
use `ucontext`):

    cd apps/win/host && make
    ./winhost scripts/tour.txt       # writes tour_*.ppm snapshots
    ./winhost scripts/menus.txt      # menus, combo box, scroll bar
    ./winhost scripts/tasks.txt      # two DEMO tasks
    rm -f HOSTWIN/PROGMAN.INI
    ./winhost scripts/progman.txt PROGMAN    # the shell: groups (MDI), items, Run, exit
    ./winhost scripts/notepad.txt 'NOTEPAD HOSTWIN\README.TXT'   # editing, Find, Save As, Open
    ./winhost800 scripts/vga800.txt  # the VGA800 driver, built in
    ./test_gdi                       # GDI sample sheet, gdi.ppm

The applications are built into the harness; `HOSTWIN/` is the Windows
directory, with empty stand-ins for their files and a "DOS program".

## Keyboard

Ctrl+Shift switches between the US and Russian (Windows-1251) layouts.
Text is ANSI Windows-1251 and is drawn with the CP866 VGA font.

## Not yet

Resources (stage 2 brings the NE loader), printing, `.GRP`
files (`PROGMAN.INI` holds the groups as text). A maximized MDI child keeps
its own caption instead of merging it into the frame's menu bar.
