# Native Windows 3.x API for murm386

An MIT-licensed implementation of the Windows 3.x API (KERNEL, GDI, USER
subset) for native ARM programs. `WINN.EXE` is the Windows loader: it holds
KERNEL, GDI, USER and the built-in drivers, loads driver modules, and runs
applications (separate EZ files) as cooperative tasks, as Windows 3.x does.
The preliminary 386 NE path is available through ordinary `WinExec` and
`WINN PROGRAM.EXE`: a Win16 program of code segments and one data segment
(the small and medium models of Open Watcom, `wcl -bt=windows -l=windows
-ms`/`-mm`) runs on the
emulator's 386 core in 16-bit protected mode with its own GDT, with its
own start-up code: its DGROUP gets the local heap and the stack of its
header, `InitTask` gives it its PSP (the command line), its instance and
nCmdShow, it ends with INT 21h 4Ch (an entry that returns with `RETF` works
too). Each 16-byte import thunk, and INT 21h, calls a common guest PM→RM→PM
gateway; the native callback runs with the original DOS Real Mode CPU
context, so WINN can service keyboard/mouse BIOS calls and cooperative
modal dialogs. After the native call, the same Win16 registers and stack
are resumed; each `RETF imm16` cleans the Win16 PASCAL arguments.

The code is not checked beforehand. It runs in ring 3 (IOPL 0): its
segments are DPL 3, its import thunks call the gateway through a call gate,
INT 21h and the yield vector below reach it through interrupt gates, and
the entry's and a window procedure's return come back through call gates
too; all of them, and every exception, switch to a ring 0 stack its own
16-bit TSS names, out of the program's reach. So a program cannot leave
protected mode, load descriptor tables, CLI, HLT or forge the gateway's
OUT, and a fault with its own stack gone (SP 0) still has a stack for its
frame. An exception goes through a private IDT back to Real Mode and DOS
and ends the program with the Windows 3.1 "Application Error" message and
the place, `segment:offset`. (The emulated CPU checks no segment limits or
write permissions, nor the DPL of a data segment loaded: within the
first megabyte a program's stores are not confined to its segments.) WINN runs the guest's instructions itself, in bursts of
CPU steps: a program that has run some 260 thousand instructions without
calling the API takes a vector of the same IDT (WINN answers the CPU's
interrupt controller callback with it for one step), whose stub calls the
gateway like an API call: the other tasks get their turn, then the program
goes on where it was. So a program busy computing does not stop the rest
of the system, as it would in Windows 3.1, and one that never returns ends
with Windows. Neither x86 CPU implementation is modified.

A program can have windows of its own classes: `RegisterClass` registers
them with a native window procedure that calls the program's (USER's
calls go through a callback entry of the trampoline, into protected mode
and ring 3 and back, nesting as the procedure calls the API in turn), and its
messages, painting and drawing are converted between the Win16 structures
in the guest and the native ones; a message's `lParam` reaches the window
procedure as it is (a native pointer, for the program to hand on to
`DefWindowProc`). Its resources are read from its file as it asks for them:
menus (`LoadMenu`, the menu of a class), accelerators, strings, dialogs
(made native templates, their dialog procedures its own code as well),
icons and cursors (`LoadIcon`, `LoadCursor`: WINN's 32x32 16-colour icons,
the group's entry closest to that, scaled; 32x32 cursors with their hot
spot), bitmaps (`LoadBitmap`: DIBs of 1, 4, 8 and 24 bits, monochrome or of
the screen's palette). Its icons and the description in its file are those
the Program Manager, Alt+Tab and the Task List show, as an EZ
application's. A class it registers with a native class's procedure
(`GetClassInfo`: a superclass) works as that class.

A Win16 program's own DLLs (NE libraries) are loaded with it, from its
directory or the Windows one: those it imports from (by name or ordinal,
and those they import from in turn) as it is loaded, others when it calls
`LoadLibrary`, `GetProcAddress` finding their exports. Their segments get
selectors of the program's own (each program its own copy of a DLL: no
data shared between programs), the exported procedures' prologue gets
their DGROUP (`MOV AX,DGROUP`, as Windows patches it), and their entries
(`LibEntry`: DS their DGROUP, DI their instance, CX their heap's size) are
called before the program's `InitTask` returns, those they need first.
`LocalAlloc` and the rest work in the caller's DS's heap (the program's
DGROUP, or a DLL's, as `LocalInit` makes it); a DLL's instance handle
finds its resources (strings, menus, dialogs, icons, bitmaps). A DLL
stays until its program ends (`FreeLibrary` frees nothing); `WEP` is not
called. A program that needs a DLL which is not there, or a function one
has not, is named in the message.

The functions a program can call: some 230 go the generic way
(`core/ne_sigs.h`, made by `sdk/gen_ne_sigs.py` from the native prototypes:
words, signed words, dwords, strings, buffers with their sizes, RECTs,
POINTs, converted and handed to the function of the native export table);
the rest have code of their own (the table in `core/ne_guest.c`): start-up
(InitTask ...), messages and window procedures, resources, dialogs,
SendMessage and PostMessage (strings for the controls made native),
SetTimer with a procedure of its own, EnumWindows, subclassing
(SetWindowLong GWL_WNDPROC, CallWindowProc), memory (GlobalAlloc: a
selector of its own per block; LocalAlloc: in DGROUP's local heap, which
grows past the stack up to 64K as in Windows: DGROUP moves, its selector
stays), OpenFile, GetDOSEnvironment, KEYBOARD's AnsiToOem, OemToAnsi (the
...Buff ones), GetKBCodePage and GetKeyboardType,
structures (TEXTMETRIC, LOGFONT, LOGPEN, LOGBRUSH, BITMAP), wsprintf,
ExtTextOut, GetTextExtentPoint, TabbedTextOut, GetTabbedTextExtent,
GetTextFace, CreateBitmap(Indirect), Get/SetBitmapBits, GetClassInfo,
_hread/_hwrite (within one block: a huge pointer's segments are not
tiled); KERNEL's GetModuleHandle, LoadLibrary and GetProcAddress for the
modules WINN has (KERNEL, USER, GDI, KEYBOARD, COMMDLG, SHELL) and its own
DLLs (below), FindResource, LoadResource, LockResource, SizeofResource,
Catch and Throw, GetTempFileName, GetWinFlags, WinExec, SetErrorMode,
FatalAppExit and the constants __AHINCR, __AHSHIFT, __WINFLAGS, __0040H
(the BIOS data, read only); palettes, CreateDC, SetSysColors; the mapping
mode is MM_TEXT, all there is (SetMapMode and the extents accept others
and keep it; DPtoLP and LPtoDP go by the origins); COMMDLG's Open and
Save dialogs, Find and Replace (its FINDREPLACE in the messages, its own),
GetFileTitle; SHELL's ShellExecute (a program, or a document by WIN.INI's
[Extensions]), FindExecutable, ShellAbout, ExtractIcon, DragAcceptFiles,
and no registration database. A
window procedure gets WM_CREATE's CREATESTRUCT, WM_GETMINMAXINFO's,
WM_WINDOWPOSCHANGING's and WM_NCCALCSIZE's structures as copies of its
own, which its DefWindowProc gives back. DOS, as its C library (Open
Watcom's) calls it, by INT 21h or DOS3Call: files (3Ch, 3Dh, 3Eh, 3Fh, 40h,
42h, 5Bh, 57h, 68h; IOCTL 4400h), names (41h, 43h, 56h; find 4Eh/4Fh with
the DTA, 1Ah/2Fh), directories and drives (39h, 3Ah, 3Bh, 47h, 0Eh, 19h,
36h), the date and time (2Ah, 2Ch), its PSP (51h/62h), the version (30h),
exit (4Ch, INT 20h), and a few that need nothing (25h/35h: no handlers of
its own, 33h, 59h, 67h): done with WINN's own file functions, its files
closed when its task ends; the standard handles 0-4 are no console (a
write goes nowhere, a read gets nothing). Its FAR ANSI strings are copied into bounded native buffers. A program
importing anything else is refused before protected mode with the first
function it lacks named; an INT 21h function WINN does not have ends it
with that function named. Several NE programs can run at once, each as a
WINN task: while one waits in a native call (a modal MessageBox), another
starts or goes on.

`host/mkne_winexec.py`, `host/mkne_dialog.py`, `host/mkne_loop.py`,
`host/mkne_fault.py` (`--hang` for `NEHANG.EXE`) and `host/mkne_watcom.py`
generate `NEAPP.EXE`, `NEDIALOG.EXE`, `NELOOP.EXE`, `NEFAULT.EXE`,
`NEHANG.EXE` and the `OW*.EXE`, built by Open Watcom, their sources in the
script: `OWHELLO`, `OWBUSY`, `OWWIN`, `OWMENU` (resources), `OWSEGS` (three
code segments), `OWDEMO` (many functions at once), `OWFILE` and `OWFILEL`
(DOS through the C library, small and large model), `OWRES` (icons, a
cursor, bitmaps, text, a superclass), `OWKERN` (KERNEL, COMMDLG, SHELL),
`OWUSE` with `OWDLL.DLL` and `OWDLL2.DLL` (its DLLs) (`make NEFIXTURES` in
`host/`). All have DOS MZ stubs that explain how to launch them under WINN
when invoked directly from DOS. `host/test_ne_run` runs them on the
emulator's own i386 core (`src/i386.c` as it is, built for the host with
the stand-ins in `host/x86run/`), and so does the host `winhost`: they are
in its `HOSTWIN/` (`scripts/owwin.txt`, `scripts/owmenu.txt`,
`scripts/owdemo.txt`, `scripts/owfile.txt`, `scripts/owres.txt`,
`scripts/owkern.txt`, `scripts/owdll.txt`).

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
`core/exports.c` and `sdk/winapi_stubs.S`, both committed), then
`sdk/gen_ne_sigs.py` (`core/ne_sigs.h`: how Win16 programs reach it).

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
    ./winhost scripts/mdimax.txt PROGMAN     # a maximized group in the menu bar, Alt+-
    ./winhost scripts/switch.txt PROGMAN     # Alt+Tab, Alt+Esc past a MessageBox; DEMO's Alt+letters
    ./winhost scripts/owwin.txt PROGMAN      # a Win16 program's window, on the i386 core
    ./winhost scripts/owmenu.txt PROGMAN     # its menu, accelerator, string, dialog
    ./winhost scripts/owdemo.txt PROGMAN     # controls, GDI, timers, memory, a file, subclassing
    ./winhost scripts/owfile.txt PROGMAN     # its C library's DOS calls: files, find, directory, date
    ./winhost scripts/owres.txt PROGMAN      # its icons, cursor, bitmaps; text; a superclass
    ./winhost scripts/owkern.txt PROGMAN     # Catch/Throw, GetProcAddress, Open, Find, ShellAbout
    ./winhost scripts/owdll.txt PROGMAN      # its DLLs: imports, LoadLibrary, their data, heap, class
    ./winhost scripts/winmine.txt WINMINE    # marks, a lost game, Expert, Best Times
    ./winhost scripts/logo.txt PROGMAN       # the logo (copy the BMPs into HOSTWIN first)
    ./winhost800 scripts/vga800.txt  # the VGA800 driver, built in
    ./test_gdi                       # GDI sample sheet, gdi.ppm
    make check                       # NE: loader, gateway, trampoline; the programs on the i386 core

The applications are built into the harness; `HOSTWIN/` is the Windows
directory, with empty stand-ins for their files and a "DOS program". On
the host, drive `C:` is the harness directory itself.

## Keyboard

Alt+Tab and Alt+Esc switch between applications (to one waiting in a
modal dialog: to the dialog); Ctrl+Esc (or a double click on
the desktop) opens the Task List, `TASKMAN.EXE` or SYSTEM.INI [boot]
`taskman.exe`, whose Cascade, Tile and Arrange Icons work on the desktop
through `CascadeChildWindows`, `TileChildWindows` and
`ArrangeIconicWindows`.

Ctrl+Shift switches between the US and Russian (Windows-1251) layouts.
Text is ANSI Windows-1251 and is drawn with the CP866 VGA font.

## Not yet

Resources (stage 2 brings the NE loader), printing, `.GRP`
files (`PROGMAN.INI` holds the groups as text), DLLs (`LoadLibrary`) of
native applications.
