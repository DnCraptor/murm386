# Native Windows 3.x API for murm386

An MIT-licensed implementation of the Windows 3.x API (KERNEL, GDI, USER
subset) for native ARM programs. `WINN.EXE` is the Windows loader: it holds
KERNEL, GDI, USER and the built-in drivers, loads driver modules, and runs
applications (separate EZ files) as cooperative tasks, as Windows 3.x does.
Stage 2 will load Win16 NE executables on the 386 core and route their
imports to the same export tables.

    WINN [:] [program [arguments]]

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
| `../winapps/<name>/` | applications: `progman` (the Program Manager, the default shell), `winfile` (File Manager), `taskman` (Task List), `winmine` (Minesweeper), `notepad`, `clock`, `calc`, `control` (Control Panel), `demo` (a tour of the API) |
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

What a Win16 program keeps in its resources, its name and its icons, an
application gives `win_app()`:

    win_app(WINMINE winmine.c DESCRIPTION "Minesweeper" ICON winmine.icon)
    win_app(CONTROL control.c DESCRIPTION "Control Panel"
            ICON control.icon color.icon desktop.icon mouse.icon datetime.icon)

An icon is a text file, 32 lines of 32 characters: `.` transparent, `0`-`F`
the 16 VGA colours; the first is the program's (up to 16). `winapp.cmake`
turns them into a `winapp_info` block whose address follows the signature
in `__ez_start`. `GetAppInfoN`, `ExtractIconN` and `ExtractIconExN` (the
n-th, or the count) read it from a file without loading the program (the
Program Manager's names and icons); `LoadIcon(hInstance,
MAKEINTRESOURCE(n))` gives a running program its n-th icon: 1 for its class
(minimized windows, Alt+Tab), the Control Panel's applets 2 to 5.
`DestroyIcon` frees one that `ExtractIcon*N` made.

Tasks switch only in `GetMessage`, `PeekMessage`, `Yield` and `WinExec`.
Every instance has its own copy of the program, so window classes belong to
the instance that registered them.

Files and disks beyond KERNEL's `_l*` functions are native WINN calls:
`WinFindFilesN` (entries with attributes, sizes and times),
`DeleteFileN`, `MoveFileN`, `CreateDirectoryN`, `RemoveDirectoryN`,
`GetDiskFreeSpaceN`, `GetCurrentDirectoryN`; `GetDriveType` is KERNEL's.
DOS critical errors (a drive without a disk) fail the call instead of
asking "Abort, Retry, Fail?".

After adding an API function, run `sdk/gen_exports.py` (it updates
`core/exports.c` and `sdk/winapi_stubs.S`, both committed).

## Start-up logo

While the shell loads, WINN shows `WINLOGO.BMP` (256 colours) or
`WINLOG16.BMP` (16-colour displays) from its directory, centred, scaled
down if it is larger than the screen. The shell starts and paints behind
it, into a copy of the screen; when the system is first idle, and the logo
has been up for SYSTEM.INI [boot] `logo.time` milliseconds (2000; 0: no
logo), the copy replaces it at once. `WINN :`
starts without it, as `WIN :` did. The files are 8 bpp BMPs, plain or
RLE8, of at most 236 colours (they take the free middle of the palette
while the logo is up); `logo/mklogo.py` makes both from `logo/logo.png`.

## Settings

`WIN.INI` next to `WINN.EXE` is read at start, as in Windows 3.1:
`[colors]` (the system colours, `Name=r g b`), `[Desktop] Pattern` (eight
row bytes, or `(None)`), `[windows] DoubleClickSpeed` and
`SwapMouseButtons`. Control Panel (`CONTROL.EXE`) edits them and applies
them at once through `SetSysColors` and `SystemParametersInfo`
(`SPI_SETDESKPATTERN`, `SPI_SETDOUBLECLICKTIME`, `SPI_SETMOUSEBUTTONSWAP`,
`SPIF_UPDATEINIFILE`); its own `CONTROL.INI` keeps the colour schemes
(`[color schemes]`, 21 colours as hex `RRGGBB` in `COLOR_` order) and the
patterns (`[Patterns]`, the twelve of Windows 3.1 written on first use).
Date/Time sets the DOS clock with `SetLocalTimeN`.

In a dialog, Enter presses the button drawn as the default one: the
focused push button, or else the dialog's default (`DM_GETDEFID`, `IDOK`
without one). The default follows the focus however it moves (Tab, a
mouse click, `SetFocus`); a control that asks for Enter in
`WM_GETDLGCODE` (an open drop-down list, an `ES_WANTRETURN` edit) keeps it.

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
    ./winhost scripts/clock.txt CLOCK        # analog, digital
    ./winhost scripts/calc.txt CALC          # arithmetic, memory, Error
    ./winhost scripts/control.txt CONTROL    # colours, pattern, schemes (writes HOSTWIN/*.INI)
    ./winhost scripts/winfile.txt WINFILE    # tree, copy, move, rename, delete in HOSTWIN/DOCS
    ./winhost scripts/taskman.txt PROGMAN    # Task List: Ctrl+Esc, Switch To, Tile, End Task
    ./winhost scripts/winmine.txt WINMINE    # marks, a lost game, Expert, Best Times
    ./winhost scripts/logo.txt PROGMAN       # the logo (copy the BMPs into HOSTWIN first)
    ./winhost800 scripts/vga800.txt  # the VGA800 driver, built in
    ./test_gdi                       # GDI sample sheet, gdi.ppm

The applications are built into the harness; `HOSTWIN/` is the Windows
directory, with empty stand-ins for their files and a "DOS program". On
the host, drive `C:` is the harness directory itself.

## Keyboard

Alt+Tab and Alt+Esc switch between windows; Ctrl+Esc (or a double click on
the desktop) opens the Task List, `TASKMAN.EXE` or SYSTEM.INI [boot]
`taskman.exe`, whose Cascade, Tile and Arrange Icons work on the desktop
through `CascadeChildWindows`, `TileChildWindows` and
`ArrangeIconicWindows`.

Ctrl+Shift switches between the US and Russian (Windows-1251) layouts.
Text is ANSI Windows-1251 and is drawn with the CP866 VGA font.

## Not yet

Resources (stage 2 brings the NE loader), printing, `.GRP`
files (`PROGMAN.INI` holds the groups as text). A maximized MDI child keeps
its own caption instead of merging it into the frame's menu bar.
