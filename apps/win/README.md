# Native Windows 3.x API for murm386

An MIT-licensed implementation of the Windows 3.x API (KERNEL, GDI, USER
subset) for native ARM programs. `WIN.COM` loads the adjacent `WINN.EXE`
into the native heap, which uses QSPI PSRAM first. `WINN.EXE` holds
KERNEL, GDI, USER and the built-in drivers, loads driver modules, and runs
applications (separate EZ files) as cooperative tasks, as Windows 3.x does.
The preliminary 386 NE path is available through ordinary `WinExec` and
`WIN PROGRAM.EXE`: a Win16 program of code segments and one data segment
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

A program can have windows of its own classes (by a name, or an atom's):
`RegisterClass` registers them with a native window procedure that calls the program's (USER's
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
the screen's palette); names RC numbered in its RT_NAMETABLE (Windows 3.0),
resources' and types' (Taipei finds its "LAYOUT" so). Its icons and the description in its file are those
the Program Manager, Alt+Tab and the Task List show, as an EZ
application's. A class it registers with a native class's procedure
(`GetClassInfo`: a superclass) works as that class.

A self-loading program (NE flag 0800h: Word for Windows 1.x, Excel 2.x)
loads itself, as Windows lets it: WINN loads and relocates its first
segment, the loader (a `SELFLOADHEADER` at its offset 0), and gives each
other segment a selector (up to 1024 segments: Word for Windows 1.0 has
196), nothing more of them is read (on disk they are the loader's, its
own format). The program starts on a stack of its own (64K less 256
bytes); WINN builds its module database (hModule: the NE header and
tables as Windows keeps them, the segment table with handles, the module
references the imported modules' handles), puts KERNEL's `MyAlloc`,
`EntryAddrProc` and `FarSetOwner` into the header, calls `BootApp(hModule,
hFile)` and then `LoadAppSeg(hModule, hFile, n)` for every segment after
the first (the file opened for each), and the handle each answers is that
segment's from then on (`MyAlloc`'s blocks are global ones, in the
segments' own selectors, `GlobalReAlloc` and the rest work on them; DGROUP
gets its local heap and stack added); then it switches to the program's
stack and enters it as any other program. Its `ExitProc` is not called.
`host/mkne_selfload.py` makes `SELFLOAD.EXE` of `OWSEGS.EXE` (201
segments, the program's scrambled on disk, its loader in the script;
`scripts/selfload.txt`).

DOS MZ and COM executables and native FDOS EZ executables can also be launched
through WinExec. They run full screen through FDOS EXEC, with the
calling task's working drive and directories. WinExec returns when
the DOS child exits, and WINN restores its display, palette and input.
Native DOS executables can use the .EXE, .EZ or .COM extension and require
QSPI PSRAM as when launched from FDOS. For a command without an extension
WinExec tries .EXE first and then .COM in its normal launch directories.

Word for Windows 1.0 (WINWORD.EXE, November 1989) runs so: it starts, asks
for the user's name, opens Document1, takes typing, its menus and dialogs
work. What it counts on, as Windows 2.x and 3.x had it: DGROUP's stack
right after its data, the local heap above it (the initial SP, its own
heap manager grows the segment); global blocks in whole paragraphs
(`GlobalSize`); `GlobalReAlloc(h, 0, GMEM_MOVEABLE)` a discard (WINN keeps
the block: no reloading); `GetModuleHandle("DISPLAY")`, whose bitmaps are
the system's (`OBM_OLD_*` too); Windows 2.x icons and cursors (no groups);
menu texts with `\b` (RC's `\a`: the right column, at a menu bar's start
the item to the right); "Switch To..." in a system menu; up to 32 window
classes; `GetParent` of a child at its `WM_NCDESTROY`;
`GetAsyncKeyState`'s "pressed since" bit. Its own dialogs do not take Esc
yet.

A Win16 program's own DLLs (NE libraries) are loaded with it, from its
directory, the Windows one or its `SYSTEM` (Windows' own NE DLLs, as
CARDS.DLL, are there): those it imports from (by name or ordinal,
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

A function of Windows' WINN does not have (of KERNEL, USER, GDI and the
rest of WINN's modules, or of Windows' own DLLs WINN has nothing of:
PENWIN, and LZEXPAND, VER, DDEML, MMSYSTEM, COMMDLG, OLECLI, OLESVR, SOUND when their DLL is
not there or has not the function) keeps no
program from loading: as Windows' "undefined dynalink" its call ends the
program, the function named ("PROG.EXE called PENWIN.REGISTERPENAPP, which
WINN does not have yet."). A program may import up to 384 of WINN's functions.

A program's PSP has its DOS environment (2Ch), made before it runs, as
DOS 3 has it: WINN's strings, the count 1, the program's path (a C
startup reads it there, without GetDOSEnvironment, which gives the same
block). LocalReAlloc keeps a block where it is when what follows it is
free, its bytes whether moved or not. A dialog template's menu, or its
class's, is the dialog's (a dialog as a program's main window); its ICON
controls show the program's icons, by name or number (a static control
takes an icon by STM_SETICON as well).

The BIOS's software interrupts a program calls itself (INT 10h, 11h, 12h,
16h, 1Ah) are answered as Windows' DOS extender would: VGA, colour (the
display combination, EGA's information, the mode), the equipment, 640K,
the keyboard's shift state (no keys come this way), the clock (ticks, the
RTC's time and date); nothing is set. So are DOS's others up to 33h
(the program's IDT has them all): the multiplex INT 2Fh (Windows 3.10 in
enhanced mode, 1600h and 160Ah; the idle calls; no XMS, no MSCDEX), DPMI
0.9's INT 31h as Windows gives it to a program (descriptors: allocate,
free, a real mode segment's, base, limit, access rights, an alias, get and
set the descriptor; DOS memory; the version, the free memory, the virtual
interrupt flag, locking; the vectors taken and kept by nobody; real mode
calls and callbacks fail with DPMI's error), the mouse's INT 33h (no DOS
driver: the program has its messages). INT 21h 66h gives or sets DOS's
code page (DOS's own; 866 on the host). A call WINN has not, or refuses, in a
procedure WINN called (a window's, a dialog's) is named as one in the
program's own code is, not a fault.

Windows' own DLLs WINN has: TOOLHELP (`core/ne_guest_sys.c`), and as
native DLLs (see Native DLLs) MMSYSTEM, VER, LZEXPAND, DDEML, COMMDLG, OLECLI,
OLESVR, SOUND. SOUND is
Windows' sound driver with one voice, the PC speaker's: a task opens it,
queues notes (1 to 84, middle C 37, of a length and dots in the voice's
tempo, mode and pitch) and tones (a frequency for clock ticks),
StartSound plays them, StopSound silences them, WaitSoundState waits for
them, CountVoiceNotes counts them; volume, envelopes, noise and
thresholds do nothing (`include/sound.h` for native callers).
OLESVR exposes server and document registration, renaming and revocation
to both Win16 and native callers. Registrations belong to their task.
Revoking a server closes its documents before releasing their resources
and the server itself; a document can revoke itself from its Close
callback, with Release deferred until Close returns.
OLESVR registers each server class as a DDEML service. An
OLE_SERVER_MULTI instance with an open document accepts connections to
that document, leaving System and OLESystem connections to free instances.
An OLE_SERVER_SINGLE instance can serve several documents.
The system topics support discovery and dispatch StdOpenDocument,
StdNewDocument, StdNewFromTemplate, StdEditDocument, StdCloseDocument and StdExit through
the server's methods. A created or registered document is available
as a named topic. A request on that topic resolves the entire document
or the named item through GetObject, copies the GetData result into DDEML,
and releases the object. This covers global-memory formats including
Native, DIB and text. Existing conversations retain their document
identity through rename. A poke on that topic passes a copy of a
global-memory payload to the target object's SetData method.
StdShowItem on the system or document topic calls the object's Show
method. StdDoVerbItem on the document topic passes the verb, visibility
and focus flags to DoVerb. StdCloseDocument closes the document associated
with the topic. An open document conversation keeps working after the
server renames the document.
DDE transport for GDI-object formats, document advises and server request
queues are still pending.
OLECLI's OleCreate uses a running server or launches the registered
StdFileEditing server with /Embedding. It creates a temporary document,
captures its Native data and optional DIB, then closes the temporary
document. The result can be stored as an embedded object. OleActivate
opens a linked document or restores an embedded object's Native data into
a temporary document, then runs the requested verb. OleUpdate refreshes
the cached DIB and, for an open embedded object, its Native data. OleClose
captures changes to an embedded object before closing its temporary
document. Closing a linked object disconnects from the source document.
OLECLI registers client documents and supports OLE 1.0 static raster
and metafile pictures. The Static protocol can paste CF_DIB and
CF_METAFILEPICT, and converts CF_BITMAP when drawing is requested.
Static streams can contain DIB, BITMAP or METAFILEPICT. BITMAP pictures
are normalized to DIB when loaded, and WMF pictures retain their vector
records through drawing, copying and saving. WMF playback also follows
map-mode changes and the metafile's SaveDC/RestoreDC stack. Win16 uses the OLECLI
ordinals and native callers use GetProcAddress with include/ole.h.
StdFileEditing can load linked and embedded OLE 1.0 objects from a
document stream and display cached DIB, BITMAP and WMF pictures. Saving
or cloning these objects retains their wire data, including the link
fields and embedded native bytes. Objects with null or unrenderable
generic previews can be retained without drawing. The generic WMF
format with a boundary-dependent trailer is unsupported. Opening the
source, updating links and editing still require the OLE server protocol.
Clients can read ObjectLink for links and OwnerLink/Native for embeddings
through OleGetData. OleSetData with ObjectLink changes the source of a
link, retaining its cached preview and update preference when saved.
OleObjectConvert can break the link by creating a static copy of the
cached picture.
Clipboard Paste and Paste Link accept OwnerLink/Native and ObjectLink
with standard picture previews. Copy publishes Native before OwnerLink
for embedded objects, and OwnerLink for linked objects. A copy replaces
the OLE formats as a group, leaving unrelated clipboard formats intact.
COMMDLG has
Windows 3.1's common dialogs: Open and Save As, Find and Replace (the
program's FINDREPLACE in their messages, kept up to date), Color (the 48
basic colours, 16 custom ones, a colour defined by hue, saturation and
luminosity or red, green and blue), Font (the faces of the system's raster
and TrueType fonts, the screen's and the "printer's" alike, their styles,
simulated or not, sizes (a TrueType face's Windows' list, 8 to 72), effects
and colour, a sample), Print (the default printer, all, the selection or
pages, copies, to a file; Setup...) and Print Setup (the default printer or
another installed one, its orientation and paper), their DC or IC,
DEVNAMES and DEVMODE (with no printer installed "No default printer", PDERR_NODEFAULTPRN), GetFileTitle, CommDlgExtendedError. The
Open and Save As dialogs have Windows 3.1's drives, Read Only and Help
too. A hook gets each message of its dialog before the dialog does,
WM_INITDIALOG after it (lParam the program's structure), FILEOKSTRING
(nonzero: the dialog stays), LBSELCHSTRING, COLOROKSTRING; the owner
gets HELPMSGSTRING as Help is pressed; SETRGBSTRING and
WM_CHOOSEFONT_GETLOGFONT are the Color's and the Font dialog's. A template
of the program's (a resource of its module, or a block of its memory)
is made with the dialog's own procedure, its controls Windows' ids
(`include/dlgs.h`); Print and Print Setup each have theirs. Native
applications call them as they are (`include/commdlg.h`; a native
caller's hooks, not its templates): WINN loads the DLL for the task at
its first call of one. MMSYSTEM's devices
are its drivers' (Volume below: wave output, MIDI, the master volume, a
mixer; no inputs, no joysticks), sndPlaySound and PlaySound (`snd.c`: a
WAV file, a [sounds] event's, an image in memory; synchronous,
asynchronous, looped; on the wave driver, the device a program opens
stopping it), the
multimedia timer (timeGetTime, timeGetSystemTime, timeGetDevCaps,
timeSetEvent: its procedure is called as the program takes its messages,
as a timer's, as often as its period went by since, ten times at most),
mmio's files (mmioOpen, Read, Write, Seek, Close,
Descend, Ascend, CreateChunk: RIFF files; unbuffered, no I/O procedures
or memory files), for native callers too (`include/mmsystem.h`);
Windows 95's MIDI streams (midiStreamOpen, Out, Property: tempo and time
division, Position in milliseconds or ticks, Pause, Restart, Stop, Close:
MIDIEVENTs played on a midiOut device's timer, tempo events, position
callbacks, SysEx skipped), DriverCallback (a window, a task, a
procedure), DriverProc's defaults, mmTaskSignal and mmTaskBlock,
mmGetCurrentTask, mmTaskYield; mmTaskCreate, mmThreadCreate and
mciLoadCommandResource answer "not supported";
TOOLHELP's TimerCount, MemManInfo and SystemHeapInfo
(the memory as GetFreeSpace has it), TaskFirst, TaskNext,
TaskFindHandle, ModuleFirst, ModuleNext, ModuleFindName and
ModuleFindHandle (WINN's modules), MemoryRead and MemoryWrite,
NotifyRegister and InterruptRegister (taken, never called); the heaps'
walks find nothing. VER's GetFileVersionInfo(Size), VerQueryValue, GetFileResource(Size),
VerLanguageName, VerFindFile and VerInstallFile (a copy, expanded by
LZEXPAND's LZCopy); LZEXPAND's files compressed by COMPRESS.EXE ("SZDD"), read expanded
(LZOpenFile finds FILE.TX_ for FILE.TXT, LZInit, LZRead, LZSeek, LZCopy,
CopyLZFile, GetExpandedName), others as they are.

When a program's call returns, a selector in its DS or ES that is no
more (a block the call freed: GlobalFree, FreeResource, ...) is 0, as
Windows' KERNEL makes it.

GetSystemDirectory is the Windows directory's `SYSTEM` when it has one
(the Windows directory itself otherwise); OpenFile (KERNEL's, native
callers' too) looks for a file in the current directory, the Windows one,
then the system one.

The clipboard's data goes between a program's global blocks and WINN's
(SetClipboardData copies the block, which is the clipboard's then and
freed; GetClipboardData gives a copy of its own, freed as the program
closes or empties the clipboard); bitmaps and palettes go as they are.

Floating point (WIN87EM): the coprocessor is the emulator's own, which
WINN turns on when it runs a Win16 program if the configuration has it
off (`[cpu] fpu=0`); it stays on afterwards, for DOS too. GetWinFlags
says there is one, so a program's coprocessor instructions stay as they
are; their OS fixups (which in Windows make them the emulator's
interrupts INT 34h to 3Dh when there is none) change nothing, or, if a
linker left the interrupts in, make them the instructions again.
WIN87EM's `__FPMATH` is a small routine of WINN's in the program's ring 3
code (by BX: initialize, the control word set and got, pop as a long,
the status word, clear, "a coprocessor": others return 0);
`__WinEm87Info`, `__WinEm87Save` and `__WinEm87Restore` do nothing (the
tasks share the coprocessor, as they share Windows' one).

The functions a program can call: some 330 go the generic way
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
stays; a fixed block's handle its pointer, a moveable one's an entry
holding its pointer, lock count and flags as Windows 3.1 has them, so a
program may read the pointer itself (`*h`, as WRITE.EXE does); LocalLock
counts, LocalReAlloc moves a block (its entry kept up to date) and
discards a moveable one (size 0), LocalFlags, LocalHandle,
LocalHandleDelta), OpenFile, GetDOSEnvironment, KEYBOARD's AnsiToOem,
OemToAnsi (the ...Buff ones), GetKBCodePage, GetKeyboardType, VkKeyScan,
OemKeyScan, MapVirtualKey, GetKeyNameText and ToAscii (the layout in use,
US or Russian), Inquire and the driver's others (doing nothing),
structures (TEXTMETRIC, LOGFONT, LOGPEN, LOGBRUSH, BITMAP), wsprintf,
ExtTextOut, GetTextExtentPoint, TabbedTextOut, GetTabbedTextExtent,
GetTextFace, CreateBitmap(Indirect), Get/SetBitmapBits, GetClassInfo,
_hread/_hwrite (a huge block's selectors each reach to its end, as
Windows' tiled ones: a read crossing 64K goes on into the next); KERNEL's GetModuleHandle, LoadLibrary and GetProcAddress for the
modules WINN has (KERNEL, USER, GDI, KEYBOARD, SHELL, WIN87EM) and its own
DLLs (below), FindResource, LoadResource, LockResource, SizeofResource,
AccessResource (the module's file opened at the resource),
Catch and Throw, GetTempFileName, GetWinFlags, WinExec, SetErrorMode,
FatalAppExit and the constants __AHINCR, __AHSHIFT, __WINFLAGS, __0040H
(the BIOS data, read only), __0000H, __A000H, __B000H, __B800H, __C000H,
__D000H, __E000H, __F000H and __ROMBIOS (the first megabyte's segments,
read only); the rest of Windows 3.1's (`core/ne_guest_more.c`):
installable drivers (OpenDriver: a key of SYSTEM.INI's [drivers], or of
the section given, names the driver's DLL and its parameters, or the name
is the file; DRV_LOAD and DRV_ENABLE as its DLL is first opened, DRV_OPEN
with the parameters each time, DRV_CLOSE, DRV_DISABLE and DRV_FREE as the
last of them closes; SendDriverMessage, DefDriverProc, GetDriverInfo,
GetNextDriver, GetDriverModuleHandle), keybd_event and mouse_event (their
arguments the registers: the input as the keyboard's and the mouse's;
GetMouseEventProc), SystemParametersInfo (all of Windows 3.1's actions,
kept in WIN.INI with SPIF_UPDATEINIFILE: the beep, the mouse's
thresholds and speed, the border, the keyboard's speed and delay, the
icons' spacing and titles, the screen saver's time, the grid, the menus'
alignment, the double click's size and time, fast task switching, the
wallpaper and the pattern), SetDeskWallPaper (a .BMP, centred or with
[Desktop] TileWallpaper=1 tiled), LongPtrAdd (the selector's base moved),
GetCodeInfo, GetModuleName, AllocResource, DirectResAlloc,
SetTaskSignalProc, SetLastError and GetLastError, LookupMenuHandle,
GetNextQueueWindow, IconSize, NoHookDosCall; SHELL's RegDeleteKey (its
subkeys too), RegEnumKey, DragQueryPoint, DoEnvironmentSubst,
FindEnvironmentString, ExtractAssociatedIcon and DriveType, KERNEL's
registry functions the same; the network's (WNet*: WN_NOT_SUPPORTED,
WNetGetCaps 0); the debugging, ROM, signal and hook functions of the
kernel and USER answered as a retail Windows 3.1 with nothing of them
does; SwitchStackTo and SwitchStackBack (the caller's frame copied to
the other stack and back, the old SS and BP in the segment's instance
data); GDI's own functions USER calls (SaveVisRgn, RestoreVisRgn,
ExcludeVisRect, IntersectVisRect, OffsetVisRgn, SelectVisRgn,
InquireVisRgn, GetClipRgn, SetDCOrg, SetDCHook, GetDCHook and
SetHookFlags: the hook kept, never called, WINN's USER keeping the DCs'
areas itself; SelectBitmap, GetPhysicalFontHandle, GetSystemIconFont,
GetTextExtentEx); and what Windows 95 added to Win16's KERNEL and USER:
Get(Private)ProfileSectionNames, CreateDirectory, RemoveDirectory,
DeleteFile, GetVersionEx (3.10), the registry's values (RegSetValueEx,
RegQueryValueEx, RegEnumValue, RegDeleteValue: a REG_SZ as it is, other
types `#type:` and hex, in WINNREG.INI), DrawTextEx, LoadImage (a module's
or the system's, or LR_LOADFROMFILE an .ICO, .CUR or .BMP file),
CopyImage, DrawIconEx, GetIconInfo, RegisterClassEx, GetClassInfoEx,
ChildWindowFromPointEx, SetPropEx, GetPropEx, RemovePropEx (32 bits: two
properties), Set/GetWindowContextHelpID, Set/GetMenuContextHelpId,
SetMessageExtraInfo; and more of Windows 95's: KERNEL's GetShortPathName
(DOS names are short already), Get/SetFileAttributes, FindFirstFile,
FindNextFile, FindClose, Get/SetCurrentDirectory, GetDiskFreeSpace,
Get/Write(Private)ProfileSection, Get/WritePrivateProfileStruct (hex
bytes and their sum, as Windows 95 writes them), IsPeFormat; USER's
FindWindowEx, Get/SetForegroundWindow, GetShellWindow, Get/SetScrollInfo
(no proportional thumbs: nPage 0), InsertMenuItem, Get/SetMenuItemInfo,
CheckMenuRadioItem, Get/SetMenuDefaultItem (kept, not drawn bold),
TrackPopupMenuEx, DrawEdge, DrawFrameControl, DrawState (a DST_COMPLEX
procedure called), DrawAnimatedRects (no animation), TileWindows,
CascadeWindows, MessageBoxEx, MessageBoxIndirect,
CreateIconFromResourceEx, LookupIconIdFromDirectoryEx, GetIconID,
CreateCursorIconIndirect, the Load*Handler resource handlers, DestroyIcon32
and the control classes' procedures (EDITWNDPROC, BUTTONWNDPROC ...);
MenuItemFromPoint, GetMenuItemRect, Set/GetWindowRgn answer "none"; GDI's
PolyBezier, PolyBezierTo, Set/GetArcDirection, ExtSelectClipRgn,
GetRegionData, ExtCreateRegion, GetTextCharset, Set/GetLayout (left to
right only), CreateHalftonePalette, EnumFontFamiliesEx (ENUMLOGFONTEX's
script, the character set chosen), GetCharacterPlacement (left to right,
a glyph a character), GetFontLanguageInfo (none), ExtCreatePen (a
cosmetic pen of its style and colour; geometric ones their width),
Get/SetMiterLimit (10), SetMetaRgn (the clip region as it is),
GetRandomRgn (SYSRGN: the DC's visible box); USER's keyboard layouts
(GetKeyboardLayout(List/Name), Load/Activate/UnloadKeyboardLayout: US
English 04090409 and Russian 04190419, WINN's two), BroadcastSystemMessage
(to the top-level windows), Change/EnumDisplaySettings (the one mode),
MsgWaitForMultipleObjects (no objects: the input or the time out),
DrawCaption, and Windows 3.1's Get/SetInternalWindowPos, DragObject
(nothing dropped), GetFilePortName, WNetErrorText, WNetEnable, WNetDisable,
WNetInitialize (WN_NOT_SUPPORTED); SHELL's ExtractIconEx (the small icons
the same ones) and InternalExtractIcon; the old entry points of functions
that moved (KERNEL's Reserved5 lstrcmp, GDI's RectInRegionOld,
GDISelectPalette, GDIRealizePalette, RealizeDefaultPalette, USER's
OldExitWindows, OldSetDeskPattern); GDI's priority queues (CreatePQ,
InsertPQ, MinPQ, ExtractPQ, SizePQ, DeletePQ) and Copy; the spooler's
OpenJob, WriteSpool, CloseJob, GetSpoolJob ... and the printer drivers'
data answer as Windows does with no printer (SP_ERROR, no job; Print
Manager's GetSpoolJob 14h list of queues empty);
palettes, CreateDC and CreateIC, SetSysColors;
the mapping modes (all of them: the metric and English ones and MM_TWIPS
at 96 pixels an inch, MM_ISOTROPIC keeping the axes alike; pens, rounded
corners and blits scale, fonts are realized at the height the mapping
mode makes, their metrics and extents in logical units), the Ex functions of the origins and extents,
DPtoLP and LPtoDP; Arc, Pie, Chord, PolyPolygon, FloodFill and
ExtFloodFill; elliptic, rounded and polygonal regions, FrameRgn,
InvertRgn, OffsetClipRgn; DIBs: SetDIBitsToDevice, StretchDIBits,
CreateDIBitmap, SetDIBits, GetDIBits, CreateDIBPatternBrush (within one
block, 1, 4, 8 or 24 bits, no RLE); metafiles: CreateMetaFile records
(Windows 3.x's records: pens, brushes, fonts, regions and palettes made
as they are selected, shapes, text, PatBlt, BitBlt and StretchBlt with
the source's pixels as a DIB, the DIB functions, the attributes, origins,
extents and clipping), CloseMetaFile, PlayMetaFile, EnumMetaFile and
PlayMetaFileRecord (the table of objects in the program's memory),
GetMetaFile (a placeable one's header skipped), CopyMetaFile,
Get/SetMetaFileBits (a global block of the program's), IsValidMetaFile;
regions painted (FillRgn and the rest) in logical coordinates, as the
mapping mode has them; fonts: Windows' raster font files (.FON, .FNT,
versions 2 and 3) WIN.INI's [fonts] names in the Windows directory or its
SYSTEM, as Windows has them ("MS Sans Serif 8,10,12,14,18,24 (VGA
res)=SSERIFE.FON"),
and those AddFontResource adds (RemoveFontResource); TrueType fonts too,
a .FOT file naming a .TTF one ("Arial (TrueType)=ARIAL.FOT", the .TTF
looked for beside it, in the Windows and the SYSTEM directory) or the .TTF
itself (only its name, metrics and character map read at the start; a
path from the root on the Windows directory's drive; WIN.INI [TrueType]
TTEnable=0 none of them, TTOnly=1 no raster files of [fonts]; a family's
regular, bold and italic files its faces, a face with the Windows-1251
letters before one without for the family's name: "Arial Cyr" for
"Arial" when Arial has none), drawn by WINN's own rasterizer (`core/truetype.c`: simple and
composite glyphs, black and white as Windows 3.1 has them on a VGA, no
smoothing; the fonts' hints run by `core/ttinterp.c`, the TrueType
instructions' interpreter: fpgm and prep once a size, each glyph's own,
the hdmx widths; the scan converter's rules, dropout control by the
font's SCANCTRL and SCANTYPE, the rasterizer Windows' and FreeType's
share, its pixels FreeType's v35 interpreter's; turned or slanted glyphs
drawn from the outline unhinted) at any height (the
em's when lfHeight is negative, the cell's when positive), Windows-1251
through the font's Unicode map (Latin-1 for a font without Cyrillic) or
its symbol one; their faces found by
name (Helv, Arial and Helvetica for MS Sans Serif, Tms Rmn, Times and
Times New Roman for MS Serif, Courier New for Courier) or by family and
pitch, the nearest size, made larger by whole numbers (up to 8 times);
else the VGA glyphs, made larger the same way; bold and italic made when
the face is not; GetTextFace the realized face's; a dialog whose template
names a font (DS_SETFONT) a font file has is laid out in that font's
units, bold as Windows 3.x has it, its controls drawn in it (the edit
controls', list boxes' and combo boxes' lines its height; MapDialogRect,
WM_GETFONT), the others as before in the system font; EnumFonts and
EnumFontFamilies (the stock fonts and the files' faces; TrueType ones once
a family, TRUETYPE_FONTTYPE, their NEWTEXTMETRIC's flags and em),
GetCharWidth, GetRasterizerCaps (TT_AVAILABLE, TT_ENABLED when there is a
TrueType font), GetOutlineTextMetrics, GetGlyphOutline (GGO_METRICS,
GGO_BITMAP, GGO_NATIVE's polygons and quadratic splines, through a MAT2),
GetFontData, GetCharABCWidths, GetKerningPairs (the kern table's),
CreateScalableFontResource (a .FOT written), LineDDA, SetTextCharacterExtra; printing
(see Printers: StartDoc, StartPage, EndPage, EndDoc, AbortDoc, Windows 3.0's
escapes); no serial or parallel
ports (OpenComm fails with IE_HARDWARE, the other comm functions with
their errors); SHELL's ShellExecute (a program, or a document by WIN.INI's
[Extensions]), FindExecutable, ShellAbout, ExtractIcon, DragAcceptFiles,
and no registration database; selectors of its own (AllocSelector,
AllocCStoDSAlias, AllocDStoCSAlias, PrestoChangoSelector, Get/Set
SelectorBase and Limit, FreeSelector: aliases, global entries with no
memory of their own), GlobalDOSAlloc, GlobalWire and the rest of the
locks, LocalHandle, LocalCompact, LocalHeapSize and their kind, lstrcpyn,
hmemcpy (within a block), IsBadReadPtr and the other IsBad*Ptr (the
selector and its limit), Yield, FatalExit, SetHandleCount, GetExpWinVer;
AllocSelectorArray, SelectorAccessRights, GlobalHandleNoRIP, lstrcatn,
GetCodeHandle, LoadModule (a program, or with -1 a library), DirectedYield,
LocalNotify and GlobalNotify (nothing is discarded), GetHeapSpaces,
GetFreeMemInfo; DlgDirList, DlgDirSelect and their combo box and Ex ones
(the subdirectories "[name]", drives "[-c-]"), Get/SetWindowPlacement,
EnumProps, LoadMenuIndirect, ChangeMenu, CreateCursor and CreateIcon
(32x32), CopyIcon, CopyCursor, ScrollWindowEx, ScrollDC, SwitchToThisWindow;
DialogBoxParam, CreateDialogParam and the Indirect ones (a template in its
memory: a far pointer, or a global block), with their WM_INITDIALOG
lParam; SetClassWord, SetClassLong (the class's extra bytes, its procedure
for its own classes), CreateWindowEx, MapWindowPoints, Get/SetKeyboardState,
EnumTaskWindows, GrayString (a string grayed; a procedure of its own draws
as it is), AnsiUpperBuff and AnsiLowerBuff, LoadBitmap(NULL, OBM_...)
(the system's bitmaps: the arrows, the caption's buttons, the check marks,
drawn at their sizes), scroll bars of SBS_TOPALIGN or SBS_BOTTOMALIGN
(LEFT/RIGHT: their standard thickness at that edge), GDI's GetDCState and
SetDCState (a DC's state kept in a DC of its own), ScanLR, KERNEL's
GetProductName and UndefDynLink (the program's end, as in Windows). Hooks (SetWindowsHook(Ex),
CallNextHookEx, DefHookProc, CallMsgFilter) are called as Windows calls
them: WH_CALLWNDPROC with a message sent to a window of the task;
WH_CBT with a window made (HCBT_CREATEWND), destroyed, activated, given
the focus, minimized or maximized, a system command (nonzero: it does not
happen); WH_KEYBOARD and WH_MOUSE with the keyboard's and the mouse's
input the task takes (not messages posted; nonzero: thrown away);
WH_GETMESSAGE with a message GetMessage or PeekMessage retrieved (the hook
may change it); WH_MSGFILTER and WH_SYSMSGFILTER with a dialog's, a
message box's, a menu's loop's messages (CallMsgFilter: MSGF_DIALOGBOX,
MSGF_MESSAGEBOX, MSGF_MENU); WH_SHELL with a top-level window made and
gone. The journals' are taken and never called. Freeing a selector
(GlobalFree, FreeSelector) clears the caller's DS and ES if they hold it, as
Windows does. A
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
importing a DLL that is not there is refused before protected mode, the
DLL named; a function WINN does not have, or an INT 21h one, ends it as it
is called, that function named. Several NE programs can run at once, each as a
WINN task: while one waits in a native call (a modal MessageBox), another
starts or goes on.

`host/mkne_winexec.py`, `host/mkne_dialog.py`, `host/mkne_loop.py`,
`host/mkne_fault.py` (`--hang` for `NEHANG.EXE`), `host/mkne_fpu.py` and
`host/mkne_watcom.py` generate `NEAPP.EXE`, `NEDIALOG.EXE`, `NELOOP.EXE`,
`NEFAULT.EXE`, `NEHANG.EXE`, `NEFPU.EXE` (the emulator's interrupts with
their OS fixups, `__FPMATH`) and the `OW*.EXE`, built by Open Watcom, their sources in the
script: `OWHELLO`, `OWBUSY`, `OWWIN`, `OWMENU` (resources), `OWSEGS` (three
code segments), `OWDEMO` (many functions at once), `OWFILE` and `OWFILEL`
(DOS through the C library, small and large model), `OWRES` (icons, a
cursor, bitmaps, text, a superclass), `OWKERN` (KERNEL, COMMDLG, SHELL),
`OWUSE` with `OWDLL.DLL` and `OWDLL2.DLL` (its DLLs), `OWFPU` (floating
point: WIN87EM), `OWAPI` (KERNEL and USER beyond the basics: selectors,
pointers, dialogs with an lParam, class words, hooks), `OWGDI` (GDI beyond
the basics: arcs, flood fill, regions, the mapping modes, DIBs, fonts
enumerated), `OWAPI2` (the clipboard, DlgDirList, window placement,
properties, menus from memory, cursors and icons of bits; OpenComm; a call
WINN has not), `OWMETA` (a metafile recorded, played, scaled, enumerated
record by record, its bits and a file), `OWSYS` (MMSYSTEM, TOOLHELP, VER,
LZEXPAND, INT 2Fh, DPMI, DDEML, the Program Manager's DDE, DDE's messages), `OWFONT` (a raster font file's faces, `WINNTEST.FON` beside
it, made by `host/mkfon.py`: its sizes, larger, bold, italic; the VGA
glyphs larger; a dialog in the font), `OWTT` (TrueType: `WINNTEST.FOT` and
`WINNTEST.TTF` beside it, made by `host/mkttf.py` from the VGA glyphs, the
face "Winn Test TT": its sizes, bold and italic made, Cyrillic, the outline
metrics, glyph bitmaps as they are, turned and wider, an outline drawn, a
.FOT made), `OWHELP` (WinHelp), `OWDLG` (COMMDLG's
Color, Font and Print dialogs, GetFileTitle), `OWHOOK` (COMMDLG's hooks
and templates), `OWDDE` (DDEML, DdeAccessData), `OWMISC` (moveable local
memory, KEYBOARD, OLECLI, OpenFile's OFSTRUCT: what WRITE.EXE needed),
`OWSND` (SOUND, OLESVR),
`OW31` (the rest of 3.1's USER, GDI, KERNEL), `OWMORE` with `OWDRV.DLL`
(an installable driver, keybd_event and mouse_event, SystemParametersInfo,
the registry enumerated, the environment, a wallpaper), `OW95` (the stack
switched, the visible area, Windows 95's KERNEL and USER for Win16),
`OW95X` (old ordinals, priority queues and the spooler, Windows 95's
structures, DrawEdge, DrawFrameControl, DrawState, Bézier curves),
`OWMIDI` (Windows 95's MIDI streams), `OWPRINT` (printing on EPSON9),
`OWLAND` (a printer driver's ExtDeviceMode, DeviceCapabilities, DEVMODE,
ResetDC), `OWMIXER` (the mixer API), `OWWAVE` (waveOut and MCI's
waveaudio on HOSTWAVE.DLL)
(`make NEFIXTURES` in
`host/`). All have DOS MZ stubs that explain how to launch them under WINN
when invoked directly from DOS. `host/test_ne_run` runs them on the
emulator's own i386 core (`src/i386.c` as it is, built for the host with
the stand-ins in `host/x86run/`), and so does the host `winhost`: they are
in its `HOSTWIN/` (`scripts/owwin.txt`, `scripts/owmenu.txt`,
`scripts/owdemo.txt`, `scripts/owfile.txt`, `scripts/owres.txt`,
`scripts/owkern.txt`, `scripts/owdll.txt`, `scripts/owfpu.txt`, `scripts/owapi.txt`, `scripts/owgdi.txt`,
`scripts/owapi2.txt`, `scripts/owmeta.txt`, `scripts/owsys.txt`, `scripts/owfont.txt`,
`scripts/owtt.txt`, `scripts/owmore.txt`, `scripts/ow95.txt`).

Win32 programs (PE, the 386's: Win32s', Windows 95's) run on the same core
(`core/pe.c`, `core/pe_api.c`, `core/pe_user.c`, the executor in
`core/ne_exec.c`): in ring 3, in a flat 4G code and data segment whose base
is where WINN put the image less the address it was linked for (so an EXE
without relocations, as most are, runs where WINN has the memory; its
DLLs, PE as well, are relocated), FS its thread's information block (the
exception handlers' chain, the TLS slots); its stack, image and heaps are
extended memory. Its calls of KERNEL32's, USER32's and GDI32's functions
are thunks of WINN's through a 386 call gate to the gateway (32-bit: the
result in EAX), its procedures are called back with the arguments on its
stack (stdcall); its time slice is ended by the yield vector (a 386
interrupt gate), its faults name the module and the offset. Most functions
are WINN's native ones as they are (the program's pointers are WINN's:
nothing is copied; `sdk/gen_pe_sigs.py` writes `core/pe_sigs.h` from
`sdk/win32.ord` and the native prototypes), the others have code of their
own: the process (its command line, environment, modules, LoadLibrary and
GetProcAddress, DllMain), memory (VirtualAlloc, heaps of its own in its
memory, Global and Local), files (CreateFile and the rest, FindFirstFile),
time, code pages and strings, TLS, events and critical sections (one
thread), resources (FindResource; its menus, dialogs (DLGTEMPLATE and
DLGTEMPLATEEX), strings, accelerators, icons, cursors and bitmaps), window
classes and subclassing (a native procedure it holds is a thunk it may
call), messages (Win32's parameters converted to and from Windows 3.1's:
WM_COMMAND's, the scroll bars', WM_CTLCOLOR*, the controls' messages'
numbers; a CREATESTRUCT, WINDOWPOS, DRAWITEMSTRUCT as copies of its
layout), painting, wsprintf, TEXTMETRIC, GetObject; DIB sections
(CreateDIBSection: the bits in its memory, given to the bitmap before it is
blitted), WinG (WING32.DLL: its DCs, top-down 8-bit bitmaps as DIB
sections, WinGBitBlt and WinGStretchBlt); WINMM, the native MMSYSTEM.DLL's
functions (the time, MCI's strings, sounds, mmio, multimedia timers); its
devices (waveOut, midiOut, aux, mciSendCommand ...), which MMSYSTEM.DLL has
for Win16 callers only, by the DLL's own Win16 entry: the program's
arguments made a Win16 caller's, its far pointers its flat addresses
(`win_dll_call_flat`; WAVEHDR, MIDIHDR, WAVEFORMAT and MCI's structures
have the same layout in both; the capabilities, MMTIME and MCI_PUT's,
MCI_WHERE's RECT converted), its callbacks (a function's, a window's
MM_WOM_DONE with its own WAVEHDR) and buffers past 64K. Structured
exceptions: a fault in ring 3 (the vectors below 12h) goes by the fault
stubs' service, RaiseException by its call, to the program's own
dispatcher (made at the first one, on its stack: EXCEPTION_RECORD,
CONTEXT): the handlers of the chain at FS:[0] in turn (asked of WINN one
by one, so a handler that never returns, an __except block after
RtlUnwind, leaves nothing of WINN's behind), EXCEPTION_CONTINUE_EXECUTION
loads the CONTEXT; RtlUnwind calls those above its target frame
(EXCEPTION_UNWINDING) and goes on at its target IP; none to handle it, the
program's SetUnhandledExceptionFilter, then the Application Error. INT 3
has its own address (as NT), a privileged instruction its code. Win32s' other
DLLs (`core/pe_dlls.c`): ADVAPI32's registry (WINN's, `core/winreg.c`:
HKEY_CLASSES_ROOT is shared with Win16. WINN reads the Windows 3.1/3.11
`REG.DAT` in its Windows directory at startup. `WINNREG.INI` overrides
individual values and adds keys. Writes and deletions go to
`WINNREG.INI` only. A missing or invalid `REG.DAT` leaves the INI
registry available. HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE and the other
Win32 roots live in the INI and support values of any type) and GetUserName;
COMDLG32, the native
COMMDLG.DLL's dialogs (Open, Save, Color, Font, Print; Find and Replace
modeless, their registered message's lParam the program's FINDREPLACE;
hooks and templates as Win16 programs', the structures in the hooks'
messages the program's own); SHELL32's ShellExecute, FindExecutable
(WIN.INI's [Extensions]), ShellAbout, ExtractIcon; LZ32, LZEXPAND.DLL's;
VERSION: a file's VERSIONINFO, Win16's and Win32's (Unicode: VerQueryValueA's
strings ANSI, kept after the block, whose size GetFileVersionInfoSizeA
doubles). And of KERNEL32, USER32 and GDI32 what Win32s has besides:
OpenFile, profile sections and structures, CreateProcess (WinExec: its
process handle signaled when the task ends), WaitForMultipleObjects,
semaphores; USER32's DDEML (DDEML.DLL's, the callback's arguments
Win32's); hooks (SetWindowsHookEx: the task's own, with Win32's
structures; CallNextHookEx), the clipboard (the program's HGLOBALs copied),
the keyboard's functions, accelerator tables, icons from resources,
GrayString, LoadMenuIndirect, MessageBoxIndirect; PolyPolygon,
EnumFontFamilies, LineDDA, GetGlyphOutline, StartDoc, ExtCreateRegion ...
`ShowWindow`'s SW_SHOWDEFAULT, which Win32 start-up code passes, is
SW_SHOWNORMAL. A function WINN does
not have ends it as it is called, named; a DLL that is not there is named
before it starts. `host/mkpe_watcom.py` writes `OW32.EXE` and
`OW32DLL.DLL` (Open Watcom's `nt_win` and `nt_dll`, their sources in the
script; `scripts/ow32.txt`), and `OW32S.EXE`, Win32s' DLLs and functions
(`scripts/ow32s.txt`), `OW32X.EXE`, structured exceptions
(`scripts/ow32x.txt`), `OW32W.EXE`, WINMM's devices (`scripts/ow32w.txt`),
`OW32H.EXE`, COMDLG32's hooks and templates (`scripts/ow32h.txt`),
`OW32D.EXE`, USER32's DDEML (`scripts/ow32d.txt`).

    WIN [:] [program [arguments]]

Install `WIN.COM` and `WINN.EXE` together in the Windows directory.
When installing over an existing Windows 3.x directory, replace its
`WIN.COM`. `LH WIN` can place the launcher in an available UMB. Use the
matching firmware for a shared PSRAM allocation map between XMS, WINN and
its native DOS children.

`WIN.INI` next to `WINN.EXE` (see the sample here) holds what programs
keep with GetProfileString and WriteProfileString: [windows] (the mouse),
[Desktop] (the pattern, the wallpaper), [colors], [fonts] (font files) and
[Extensions], the documents' programs ("hlp=winhelp.exe ^.hlp"): the File
Manager, the Program Manager (its items, Run) and ShellExecute open a
document with them. Without it each setting has its default (text files
open in Notepad).

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
| `user/` | windows, messages, painting, non-client area, menus and accelerators, controls (BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR), dialogs, MessageBox, MDI (MDICLIENT, DefFrameProc, DefMDIChildProc), clipboard, the common dialogs' entries for native applications (COMMDLG.DLL's, loaded for them) |
| `drivers/dos/` | display drivers `VGA` (mode 12h 640x480 in 16 colours, the default; `planar16.h`, shared with VGA800.DRV) and `VBE8` (VBE 100h 640x400x8, fallback 13h), the `DOS` input driver (keyboard through the BIOS buffer, INT 33h mouse), the `SPEAKER` sound driver (the PC speaker through PIT channel 2: MessageBeep, SOUND.DLL; `core/sound.c` times the notes) |
| `runtime/` | `WINN.EXE` `main()`: drivers, core, shell and command-line program as tasks |
| `sdk/` | for applications: API stubs by Win16 ordinal, `__ez_start`, `winapp.cmake`; `gen_exports.py` makes the export tables; `gen_ne_sigs.py` and `gen_pe_sigs.py` (with `win32.ord`) the Win16 and Win32 programs' generic calls |
| `core/ne*.c`, `core/pe*.c` | Win16 (NE) and Win32 (PE) programs and DLLs on the emulator's 386 core: the loaders, the trampoline and gateway (`ne_boot.c`), the executor (`ne_exec.c`), their calls of WINN's functions |
| `../winapps/<name>/` | applications: `progman` (the Program Manager, the default shell), `winfile` (File Manager), `taskman` (Task List), `winmine` (Minesweeper), `winhelp` (Windows Help), `notepad`, `clock`, `calc`, `control` (Control Panel), `demo` (a tour of the API) |
| `../windrv/<name>/` | driver modules; `vga800` is VBE 102h 800x600 in 16 colours |
| `ddi/windll.h`, `core/windll.c` | native DLLs: the interface, and their loading, uses and unloading |
| `../windll/<name>/` | native DLLs: `lzexpand` (LZEXPAND.DLL), `ver` (VER.DLL), `ddeml` (DDEML.DLL), `mmsystem` (MMSYSTEM.DLL), `commdlg` (COMMDLG.DLL), `olecli` (OLECLI.DLL), `olesvr` (OLESVR.DLL), `sound` (SOUND.DLL), `epson9` (EPSON9.DLL, a printer driver) |
| `host/` | the same sources on Linux with a snapshot display and scripted input |

## Drivers

The core never touches video hardware. A display driver reports a
framebuffer as a surface (pointer, size, pitch, bpp), accepts palette
updates and is told which rectangles changed. Another mode or another
device is another driver; the core works at any size.

The built-in default is `VGA`: mode 12h, 640x480 in 16 colours, as
Windows 3.x's own VGA.DRV, the size Windows programs are made for. Like
`VGA800.DRV` (both are `drivers/dos/planar16.h`) it keeps an 8 bpp copy
of the screen and converts changed rectangles to the four VGA planes,
mapping colours to the standard 16. If it cannot start, or SYSTEM.INI's
[display] asks for another size (`height=400`), the other built-in one is
used: `VBE8`, VBE 100h, 640x400 in 256 colours, drawn straight into the
video memory (101h, 640x480x8, does not fit its 256 KB). On an HDMI
build the firmware shows VBE 100h with every second pixel of a line, and
the colours 252..255 (white, cyan, magenta, blue in Windows' palette) as
251: the HDMI signal's own symbols take those indices; 12h and VBE 102h
it shows in full.

Drivers can be separate files. A module is an EZ image built without crt0
whose `__ez_start` takes the core's service table and returns its driver
tables; the core reads it, relocates it and calls it itself, no firmware
support involved. `SYSTEM.INI` next to `WINN.EXE` names the modules:

    [boot]
    display.drv=VGA800.DRV

Without the file, or if a module fails to load, the built-in drivers are
used.

## Printers

A printer driver is a native DLL exporting `WinPrinterDriver`
(`include/printer_driver.h`): its models, its resolution, its papers, and
the three calls that turn pages into the printer's language. GDI draws a
page into a monochrome bitmap at the driver's resolution (`CreateDC(driver,
device, port, DEVMODE)`: GetDeviceCaps gives the page's size, dots an inch,
DT_RASPRINTER; the mapping modes go by its dots an inch), EndPage hands it
to the driver (a landscape page turned upright, each page as many times as
the copies), which writes it to the port: `FILE:` asks for a file's name
at each printing, `LPTn:`, `COMn:` are DOS's devices, any other name a
file.

The printer's settings are Windows 3.1's DEVMODE (the paper, the
orientation, the copies), GDI's for every driver, kept in WIN.INI's
`[<device>,<port>]` (Orientation=, PaperSize=). A Win16 program asks the
driver for them as it does Windows' drivers: `LoadLibrary("EPSON9.DRV")`
(the native DLL), ExtDeviceMode (its size, DM_COPY, DM_MODIFY, DM_UPDATE,
DM_PROMPT: the driver's dialog, Paper Size and Orientation),
DeviceCapabilities (DC_PAPERS, DC_PAPERNAMES, DC_PAPERSIZE,
DC_ENUMRESOLUTIONS, DC_COPIES, DC_ORIENTATION ...), DeviceMode,
AdvancedSetUpDialog; native programs call `PrinterDeviceModeN` and
`PrinterCapabilitiesN`. ResetDC changes them between pages (another paper:
the driver starts again on it), GETSETPRINTORIENT and SETCOPYCOUNT too.
COMMDLG's PrintDlg returns hDevMode (and takes the program's), its Print
Setup has Orientation and Paper Size, PD_USEDEVMODECOPIES leaves the
copies to the driver. WIN.INI keeps the printers as Windows 3.1 does ([devices],
[PrinterPorts], [windows] device=, [ports]); the Control Panel's Printers
adds them from the drivers' models (CONTROL.INI [printer drivers]
drivers=, EPSON9 when none), connects them to a port, sets the default.

`EPSON9.DLL` (`apps/windll/epson9`) prints on Epson's 9-pin dot matrix
printers (ESC/P: LX-350, LX-300, LX-800, FX-850, FX-1050): 72 x 72 dots an
inch, 8 inches across (80 columns of continuous paper) on Letter, A4,
Legal and German fanfold (the length set by ESC C), bands of the
one-to-one bit image (ESC * 5), blank bands as line feeds. The Control
Panel's Printers has its Setup... (the driver's dialog).

## Wave output

`SYSTEM.INI` [drivers] wave= names the wave driver, a native DLL exporting
`WinWaveDriver` (`include/wave_driver.h`): `SBWAVE.DLL` plays on the
emulator's Sound Blaster 16. The blocks are queued, the next submitted
while one plays: waveOut fills each from the program's headers in turn
(a block may end one and begin the next), MCI's waveaudio from the file.
SBWAVE puts them one after another into a ring the DMA goes round
(auto-initialised) and gives the DSP all that is there as one transfer
that ends by itself (auto-init, then DA/D9), the next as soon as it ends,
while the SB16 still plays what it has buffered: they play without a gap,
and the SB16, which reads ahead of what it plays, never reads past the
samples (a single-cycle transfer a block, as before, dropped what the
SB16 had buffered of the one before). `host/test_sbwave` runs it on a
model of the emulator's SB16 and DMA. The host's tests use
`HOSTWAVE.DLL` (`host/host_wave.c`): its blocks on stderr, the samples in
HOSTWAVE.RAW.

## Video for Windows

MCI's AVIVideo (`mci_avi.c` in MMSYSTEM; SYSTEM.INI [mci] AVIVideo=,
[mci extensions] avi=) plays AVI files of the codecs Windows 3.1 came with:
uncompressed DIBs (8, 16, 24 bits), RLE8 and Microsoft Video 1 (CRAM, 8
and 16 bits), their sound (PCM) on the wave driver as MCI's waveaudio's.
A file of another codec (Indeo, Cinepak) plays its sound, the picture
black, after Windows' warning ("cannot find 'vids:iv32' decompressor").
16- and 24-bit pictures are drawn in a 6x6x6 colour cube. The frames go
into the default window (below the program's, shown when it plays, not
activated) or the program's (MCI_WINDOW), at the destination (MCI_PUT,
MCI_WHERE, MCI_UPDATE), as their time comes: a late one is decoded, not
drawn. Frames (the default) or milliseconds; play from, to, repeat,
notify, wait; pause, resume, seek, step; the device opened without a file
and MCI_LOAD (Media Player's Device menu); MCI's strings as well.

## Volume

MMSYSTEM keeps three volumes in SYSTEM.INI [volume]: the master one (the
auxiliary device 0, "Master Volume"), the wave output's and the MIDI
output's (waveOut/midiOut/auxGet/SetVolume, left and right; the
capabilities say so). The drivers have none of their own: the samples are
scaled on their way to the wave driver (MCI's too), MIDI notes by their
velocity; each can be muted. SYSTEM.INI is written when a program ends,
a mixer is closed or MMSYSTEM is unloaded, not at each change (a program
fading its sound sets the volume tens of thousands of times).

The mixer (Windows 95's API, `mixer.c`; Win16 and native) is one device:
its destination, the speakers, is the master volume, its sources the wave
output and the synthesizer, each line a volume (left, right) and a mute
control (mixerGetLineInfo by destination, source, line ID, component
type, target type; mixerGetLineControls all, one by ID, one by type;
mixerGet/SetControlDetails). They are the same volumes, so whoever
changes one (waveOutSetVolume, auxSetVolume, the Control Panel), the
mixer's windows are told MM_MIXM_CONTROL_CHANGE. The Control Panel's
Volume is on the mixer: it sets the volumes as they are dragged, the
mutes, and shows other programs' changes.

## Native DLLs

Windows' own libraries are native DLLs of WINN's, loaded when a program
needs one and unloaded when none does any more, as Windows 3.x does with
its DLLs, instead of being built into `WINN.EXE`: `LZEXPAND.DLL`,
`VER.DLL` (it reads a file's resources with the core's
`GetFileResourceN`), `DDEML.DLL`, `MMSYSTEM.DLL`, `COMMDLG.DLL`, `OLECLI.DLL`,
`OLESVR.DLL`, `SOUND.DLL` (WINN's speaker: `SoundQueueN` and the rest),
`SYSTEM.DLL` (SYSTEM.DRV's: InquireSystem, the system timers called as
their task takes its messages, GetSystemMSecCount, the 80x87's state
size) and `MOUSE.DLL` (MOUSE.DRV's Inquire). A program importing one that is not there, or a
function the DLL has not, still loads; the call ends it.
A native DLL is an EZ image built with `sdk/windll.cmake` (no crt0; its
`__ez_start`, from `sdk/windll_entry.c`, begins with a branch over the
signature `NWDL`) whose `WinDllInit` gives its exports: the Win16
ordinal and name programs import each by, the bytes of a Win16 caller's
PASCAL arguments, the native function (fewer than 1024 exports: a Win16
program's call of one is the export's place in the table, whatever its
ordinal). It calls KERNEL, GDI and USER
through the API stubs as an application does, and has Win16 programs'
calls of its functions with their arguments and memory from the core
(`win_dll_ne`), which also calls a procedure of a task's Win16 program
for it (a callback), reads and writes a task's Win16 program's memory
outside its calls, sends a message to a procedure of a task's Win16
program as USER would (a dialog's hook), makes a dialog of a template of
the program's with the DLL's dialog procedure, and asks the DLL for the
lParam a Win16 window gets with a registered message one of the DLL's
windows sends (COMMDLG's: the program's own FINDREPLACE, OPENFILENAME
...).

DDEML (`include/ddeml.h` for native callers) is the DDE Management
Library between the programs WINN runs, native, Win16 and Win32 (USER32's
Dde* functions) alike:
instances with their callbacks and filters, services (DdeNameService,
XTYP_REGISTER), conversations (DdeConnect asks the servers' callbacks with
XTYP_CONNECT), the client's request, poke, execute and advise links
(DdePostAdvise: XTYP_ADVREQ, XTYP_ADVDATA), asynchronous transactions
(XTYP_XACT_COMPLETE as the client takes its messages), data handles and
string handles (global atoms). An instance's callback is called in its
task, through a hidden window of the instance. DdeEnableCallback controls
blockable transactions. A transaction that returns CBR_BLOCK remains in
the queue until the conversation is enabled. EC_QUERYWAITING inspects the
queue and EC_ENABLEONE allows one callback per selected conversation.
There is no NetDDE, no
conversation with a program speaking DDE's messages itself.
DdeConnectList enumerates local servers for a service/topic pair. A
wildcard service or topic asks servers for matching pairs with
XTYP_WILDCONNECT. DdeQueryNextServer walks the list, and DdeDisconnectList
ends its conversations. During XTYP_DISCONNECT the client can call
DdeReconnect to open a new conversation with the same server and ask
it to restore the advise loops. A program's DdeAccessData gives a copy
of the handle's bytes in its own memory (a Win16 program's a global block
of its),
which DdeUnaccessData takes back. The
Program Manager is a DDEML server as Windows' is for setup programs:
service and topic PROGMAN, [CreateGroup], [ShowGroup], [AddItem],
[DeleteItem], [ReplaceItem], [DeleteGroup] ([Reload], [ExitProgman] do
nothing), the item Groups; and to programs speaking DDE's messages
themselves (WM_DDE_INITIATE, EXECUTE, REQUEST, TERMINATE), as setup
programs do. DDE's global memory goes between programs as copies: a Win16
program's block (its selector) becomes a native one for the other end and
back (the ACK that returns an EXECUTE's commands gives the sender its own
block; DATA and POKE with fRelease are the receiver's). A block the
program sends again goes with what it holds then (an installer writing its
commands one after another into one block, not waiting for the ACKs).

The core looks for `<NAME>.DLL` in the Windows directory (`WINN.EXE`'s),
then its `SYSTEM`; a file there that is not a native DLL (Windows' own NE
DLL of that name) is left to the Win16 loader, which then loads that one.
There is one instance of a DLL for all tasks: what it keeps for a task it
keeps by the task and lets go at the task's end (`task_end`); `term` is
called as it is unloaded. A task uses a DLL by importing it (a Win16
program) or by `LoadLibrary`, and lets it go by `FreeLibrary` or by
ending; `GetModuleHandle` and `GetProcAddress` (by name or ordinal) find
loaded DLLs, for native callers and Win16 programs alike, TOOLHELP's
module list has them. A Win16 program's import of a function the DLL has
not (by ordinal) is named when the program calls it. On the host the DLLs
are built into `winhost` (`host/host_dlls.c`).

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
(minimized windows, Alt+Tab), the Control Panel's applets 2 to 9.
`DestroyIcon` frees one that `ExtractIcon*N` made.

Tasks switch only in `GetMessage`, `PeekMessage`, `Yield` and `WinExec`.
A `PeekMessage` that finds nothing yields to the other tasks, then gives
the system its idle turn (a program waiting in such a loop, as WRITE.EXE's
blinking caret does, keeps nothing from going on). WinExec's hourglass stays until
the program started first waits for input (or ends). Every instance has its own copy of the program, so window classes belong to
the instance that registered them.

The rest of Windows 3.1's API for both kinds of programs: the clipboard
viewers' chain (`SetClipboardViewer`: WM_DRAWCLIPBOARD as the clipboard
changes, `ChangeClipboardChain`: WM_CHANGECBCHAIN), `GetPriorityClipboardFormat`,
`GetOpenClipboardWindow`; `ClipCursor` keeps the cursor in a rectangle
(`GetClipCursor`); system timers (`SetSystemTimer`: WM_SYSTIMER);
`EnableHardwareInput`, `IsUserIdle`, `UserYield`, `EndMenu`,
`SetSystemMenu`, `WindowFromDC`, `GetDCEx`, `GetControlBrush`,
`FillWindow`, `PaintRect`, `RepaintScreen`, `SysErrorBox`, `DragDetect`,
`CalcChildScroll` and `ScrollChildren` (MDI); a DC's bounds
(`SetBoundsRect`, `GetBoundsRect`: what was drawn while enabled), the
device's pens and brushes (`EnumObjects`), `IsGDIObject`, `GetCurLogFont`,
`SetRelAbs`, `SetMapperFlags`, `GetAspectRatioFilter(Ex)`,
`ResizePalette`, `AnimatePalette` (PC_RESERVED entries, on the screen as
realized), `Set/GetSystemPaletteUse`, TrueType's (`GetRasterizerCaps`,
`GetFontData`, `GetGlyphOutline`, `GetCharABCWidths`,
`GetOutlineTextMetrics`, `GetKerningPairs`, `CreateScalableFontResource`;
a raster font has none: GDI_ERROR, 0); KERNEL's `SetPriority`,
`LockCurrentTask`, `Get/SetTaskQueue`, `PostEvent`, `InitAtomTable`,
`GetAtomHandle` and the like WINN has no use for, done as asked.

A window DC follows its window: one a
program keeps (`CS_OWNDC`, or a `GetDC` it has not released) is made new
for where the window is and what covers it when it is used after the
windows changed; after `EndPaint` a `CS_OWNDC` one has its whole visible
area again.

Files and disks beyond KERNEL's `_l*` functions are native WINN calls:
`WinFindFilesN` (entries with attributes, sizes and times),
`DeleteFileN`, `MoveFileN`, `CreateDirectoryN`, `RemoveDirectoryN`,
`GetDiskFreeSpaceN`, `GetCurrentDirectoryN`; `GetDriveType` is KERNEL's.
DOS critical errors (a drive without a disk) fail the call instead of
asking "Abort, Retry, Fail?".

Windows Help (`WINHELP.EXE`, `hlpfile.c` reading the files) shows Windows
3.1's help files (.HLP: |SYSTEM, the phrases, |TOPIC's LZ77 blocks, |FONT,
|CONTEXT, |CTXOMAP, |TTLBTREE, the keywords, the bitmaps): a topic's
paragraphs laid out in the window's width with their fonts, indents, tabs,
alignment, table rows and bitmaps (DIBs and DDBs, RLE and LZ77), the
non-scrolling region above the rest; jumps (to this file or another),
pop-ups, macro hotspots; Contents, Search (the keywords, their topics, Go
To), Back, the browse sequence's << >> (BrowseButtons), the file's own
buttons (CreateButton), [CONFIG]'s and the hotspots' macros (Contents,
Search, Back, Prev, Next, JumpId, PopupId, JumpContext, PopupContext,
JumpKeyword, JumpContents, HelpOn, ExecProgram, Exit and the buttons'
ones; the rest do nothing), Edit Copy. Windows 95's files (2K blocks,
Hall compression), metafiles, bookmarks, annotations and printing it has
not. `WinHelp` (native callers and Win16 programs alike) starts it, or
asks the running one (`WINHELP_N`): HELP_CONTEXT, HELP_CONTEXTPOPUP,
HELP_KEY, HELP_PARTIALKEY, HELP_MULTIKEY, HELP_COMMAND, HELP_CONTENTS,
HELP_HELPONHELP, HELP_QUIT. Its command line: the file, then `-n` a
[MAP] number, `-i` a context string, `-k` a keyword, `-m` a macro (`-p`
before them: as a pop-up).

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
row bytes, or `(None)`), `Wallpaper` (a .BMP file, or `(None)`) and
`TileWallpaper`, `[windows] DoubleClickSpeed` and `SwapMouseButtons`, and
what SystemParametersInfo keeps (`Beep`, `MouseThreshold1` ...). Control Panel (`CONTROL.EXE`) edits them and applies
them at once through `SetSysColors` and `SystemParametersInfo`
(`SPI_SETDESKPATTERN`, `SPI_SETDOUBLECLICKTIME`, `SPI_SETMOUSEBUTTONSWAP`,
`SPIF_UPDATEINIFILE`); its own `CONTROL.INI` keeps the colour schemes
(`[color schemes]`, 21 colours as hex `RRGGBB` in `COLOR_` order) and the
patterns (`[Patterns]`, the twelve of Windows 3.1 written on first use).
Date/Time sets the DOS clock with `SetLocalTimeN`. Sound keeps the system
events' sounds, `[sounds]` (`SystemAsterisk=chord.wav,Asterisk`: a WAV
file of the Windows directory, `<none>`; Windows 3.1's events and files
when there are none), tried with Test, and `[windows] Beep`: MessageBeep
plays the event of its type (SystemHand, SystemQuestion,
SystemExclamation, SystemAsterisk, SystemDefault), MessageBox its icon's,
ExitWindows SystemExit, through MMSYSTEM when there is a file for it
(else MessageBeep beeps the speaker; MessageBox stays silent as before);
Beep=no silences them all.

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

    cmake -S apps/win -B build-win                 # apps/compiled/{win.com,winn.exe}
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

Drive C: is the directory the test runs in, `C:` a link to it there that
winhost makes (the Windows directory `C:/HOSTWIN`, a full path for fopen
and for DOS alike: tasks run in their programs' directories); `A:` too
(a setup run from `A:\`). File Manager and the directory searches do not
list the links.

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
    ./winhost scripts/owfpu.txt PROGMAN      # floating point: sqrt, sin, exp, printf's %f (WIN87EM)
    ./winhost scripts/owapi.txt PROGMAN      # selectors, IsBad*Ptr, DialogBoxParam, class words, hooks
    ./winhost scripts/owgdi.txt PROGMAN      # arcs, FloodFill, regions, MM_ISOTROPIC, DIBs, EnumFonts
    ./winhost scripts/owapi2.txt PROGMAN     # clipboard, DlgDirList, placement, a call WINN has not
    ./winhost scripts/winmine.txt WINMINE    # marks, a lost game, Expert, Best Times
    ./winhost scripts/winhelp.txt PROGMAN    # Windows Help on WINNTEST.HLP (mkhlp.py)
    ./winhost scripts/owhelp.txt PROGMAN     # WinHelp from a Win16 program: context, keyword, quit
    ./winhost scripts/owdlg.txt PROGMAN      # COMMDLG: Color (full), Font, Print (no printer), GetFileTitle
    ./winhost scripts/owhook.txt PROGMAN     # COMMDLG's hooks and templates: Open, Color, Font, Find, Print
    ./winhost scripts/owdde.txt PROGMAN      # DDEML: a server and a client of its own, DdeAccessData
    ./winhost scripts/owmisc.txt PROGMAN     # moveable local memory, KEYBOARD, OLECLI
    ./winhost scripts/owsnd.txt PROGMAN      # SOUND (tones on stderr), OLESVR
    ./winhost scripts/ow31.txt PROGMAN       # clipboard viewers, ClipCursor, bounds, EnumObjects...
    ./winhost scripts/owtt.txt PROGMAN       # TrueType: WINNTEST.FOT/.TTF (mkttf.py), outlines, metrics
    ./winhost scripts/owmore.txt PROGMAN     # a driver (OWDRV.DLL), keybd/mouse_event, SPI, wallpaper
    ./winhost scripts/ow95.txt PROGMAN       # SwitchStackTo, the visible area, Win95's KERNEL/USER
    ./winhost scripts/ow95x.txt PROGMAN      # old ordinals, the spooler, Win95's structures and 3D drawing
    ./winhost scripts/owmidi.txt PROGMAN     # MIDI streams on HOSTMIDI.DLL (host_midi.c, notes on stderr)
    ./winhost scripts/owprint.txt PROGMAN    # printing: PrintDlg, two pages on EPSON9 to OWPRINT.PRN
    ./winhost scripts/printers.txt CONTROL   # Control Panel's Printers (add, connect, setup) and Volume
    ./winhost scripts/prsetup.txt PROGMAN    # COMMDLG's Print Setup: landscape A4, printed across
    ./winhost scripts/owland.txt PROGMAN     # ExtDeviceMode, DeviceCapabilities, DEVMODE DCs, ResetDC
    ./winhost scripts/owmixer.txt PROGMAN    # the mixer API: lines, controls, details, notifications
    ./winhost scripts/owwave.txt PROGMAN     # waveOut and MCI on HOSTWAVE.DLL: blocks without gaps
    ./winhost scripts/logo.txt PROGMAN       # the logo (copy the BMPs into HOSTWIN first)
    ./winhost800 scripts/vga800.txt  # the VGA800 driver, built in
    ./winhostvga scripts/vga.txt     # the built-in VGA driver (640x480x16), Win16 programs too
    ./test_gdi                       # GDI sample sheet, gdi.ppm
    make check                       # NE: loader, gateway, trampoline; the programs on the i386 core

The applications are built into the harness; `HOSTWIN/` is the Windows
directory, with empty stand-ins for their files and a "DOS program". On
the host, drive `C:` is the harness directory itself.

## Memory

Win16 programs run in protected mode, and their segments (code, data, local
heaps, GlobalAlloc's) are in extended memory: WINN's own heap above the HMA,
which on the target is the guest's extended memory itself (the PSRAM; XMS
hands out the same memory, so WINN takes it from its heap, not from XMS).
SYSTEM.INI [memory] `ext=` kilobytes they may have of it at most (0: none);
without the key, as much as there is. When there is none, DOS memory blocks
(the upper memory blocks first when DOS has them). What Real Mode must reach
stays below 1M: the trampoline, each program's PSP and descriptor table,
`GlobalDOSAlloc`'s and DPMI 0100h's blocks. A growing segment (a local heap)
grows where it is when it can. On the host, `WINN_EXT_KB` (decimal) limits
the extended memory (8M) and `WINN_DOS_TOP` (hex) makes DOS's memory end
lower than 640K, as a fuller DOS has it.

The segments are managed as Windows 3.x's memory manager does: a program's
and its DLLs' code segments without PRELOAD (all but the entry's) are not
loaded at the start; their descriptors are not present, and the first
use of one (a far call, a far return, a segment register loaded with it;
WINN's own access of it: a string there) takes the #NP whose gate goes to
the gateway (`WIN_NE_SERVICE_NP`): the segment is read from its file again,
its fixups applied as at the load, and the instruction runs again.
DISCARDABLE code segments go when memory runs out (an allocation that
fails: the least recently used first, of all the programs), their
descriptors not present again, and come back the same way. Ring 0 code
takes no #NP: before WINN goes back to a waiting program the selectors its
gateway loads (DS, ES, an interrupt's CS) and a callback's procedure are
in memory. GetFreeSpace and DPMI 0500h count the discardable code as free,
GlobalCompact discards until a block of its size is free. (Paging, a swap
file: none. Data and resources stay where they are.)

## Keyboard

Alt+Tab and Alt+Esc switch between applications (to one waiting in a
modal dialog: to the dialog); Ctrl+Esc (or a double click on
the desktop) opens the Task List, `TASKMAN.EXE` or SYSTEM.INI [boot]
`taskman.exe`, whose Cascade, Tile and Arrange Icons work on the desktop
through `CascadeChildWindows`, `TileChildWindows` and
`ArrangeIconicWindows`. With SYSTEM.INI [debug] `trace=1`, Ctrl+Esc
first writes `WINNTRAC.LOG` in the Windows directory: the last 256
calls Win16 programs made of WINN's (the task, the caller's CS:IP, the
call, DX:AX; one repeated from the same place counted), where their
last turns ended, and the programs' memory (extended and DOS's, DOS's
largest free block), for a program that hangs; and a line of the
TrueType fonts': their files read (ms), sizes made and kept, glyphs drawn
(ms), advances hinted, for text that draws slowly.

Ctrl+Shift switches between the US and Russian (Windows-1251) layouts.
Text is ANSI Windows-1251 and is drawn with the CP866 VGA font, or with
the fonts of the font files: their bytes as they are, so the raster files
must be the Windows-1251 ones (as a Russian Windows has them) for
Cyrillic; TrueType fonts map the characters to Unicode themselves.

## Not yet

Win32: threads (CreateThread fails), access violations (no paging: a bad
pointer reads what is there), the FPU's exceptions' own codes, the Unicode
(W) functions, what GDI draws into a DIB section (its bits are not brought
back), ADVAPI32 but the registry, WINMM's CALLBACK_EVENT and wave input,
COMCTL32, OLE32, DDEML's
Unicode functions; VxDs.
A spooler and Print Manager's queues (printing goes straight to the port),
printer drivers of Windows' own (.DRV files: WINN's are native DLLs), a
driver's private DEVMODE data, the mixer's inputs, `.GRP` files (`PROGMAN.INI` holds the groups as text), DLLs (`LoadLibrary`) of native applications;
Windows 95's and NT's additions to Win16 (WOW, the thunks to Win32,
paths, enhanced metafiles), BiDi, STRESS.DLL, the
undocumented CallProcInstance, InitLib and DoSignal (KERNEL's own, whose
arguments nothing tells) and Windows 2's GetContinuingTextExtent.
