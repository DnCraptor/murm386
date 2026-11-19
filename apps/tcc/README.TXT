Murm386 TinyCC SDK for Native FreeDOS
====================================

The SDK is installed in \TCC at the root of a FAT volume. Start Native
FreeDOS on an RP2350 with the current Murm386 firmware. Select the DOS
drive that contains \TCC. Its drive letter may differ from the letter
shown by Windows when the card was mounted there.

TCC.COM      Native ARM Thumb C compiler and relocatable linker.
ELF2EZ.COM   Converts ARM ELF object files into native EZ programs.
INCLUDE\     C, Native FreeDOS and WINN headers.
LIB\         FDOSCRT.O, WINCRT.O and the shared runtime libfdos.a.
SRC\         Example programs, tool sources and build scripts.
COPYING      TinyCC license.

Keep the firmware and SDK from the same revision. Self-building the tools
requires sufficient application PSRAM. The compiler uses long filenames
on FAT, including several of its own source and header names.

Quick check on the target
-------------------------

From the DOS drive that contains the SDK:

    CD \TCC\SRC
    ..\TCC.COM -v
    CHECK.BAT

The version line should include "ARM Thumb Native FreeDOS". CHECK.BAT
compiles, links, converts and runs the FDOS example. It creates and
reopens fdhello-smoke.o to exercise long filenames. The expected output
from the program is "Native FDOS C: FDHELLO.EXE".

The same batch also builds WNHELLO.EXE. Start the rebuilt WINN and open
this file via Program Manager's File > Run. Enter the full DOS path, for
example C:\TCC\SRC\WNHELLO.EXE when the SDK is on C:. The dialog is titled
"WINN C" and displays "Built by native TinyCC".

Building your own programs
--------------------------

From \TCC\SRC, compile one source file at a time with -c when needed.
The -r option links source files or objects into a relocatable ARM ELF
file. Use FDOSCRT.O for a program with main(), or WINCRT.O for one with
WinMain(). Put libraries after the files that use them. For example:

    ..\TCC.COM -nostdlib -c -I../INCLUDE fdhello.c
    ..\TCC.COM -nostdlib -r -o HELLO.O fdhello.o ../LIB/FDOSCRT.O -L../LIB -lfdos
    ..\ELF2EZ.COM /y --thumb2 -o HELLO.EXE HELLO.O
    HELLO.EXE

Use --thumb2 for binaries produced by this TinyCC. The EZ file runs on
RP2350. The ELF .O file is the converter's input. /y allows overwriting
an existing output file. Without -o, ELF2EZ names it after the input
with an .EXE extension.

Rebuilding the tools in Native FreeDOS
--------------------------------------

From \TCC\SRC:

    MKELF2EZ.BAT
    DIR ..\E2ESELF.COM
    SELF.BAT
    ..\TCCSELF.COM -v

MKELF2EZ.BAT compiles ELF2EZ.C, links it with FDOSCRT.O and libfdos.a,
and converts it to \TCC\E2ESELF.COM. Look for the EZ image summary.

SELF.BAT compiles the TinyCC source files listed in COMPILE.RSP, links the
objects in LINK.RSP into TCCSELF.O, then converts that object into
\TCC\TCCSELF.COM. It prints each source filename during compilation.
The final line should be "Built ..\TCCSELF.COM". A leftover .COM from an
earlier run can still appear in DIR after a failed build; check the
current run's output and file dates.

Verify both freshly rebuilt tools by generating and running another
FDOS example from the same directory:

    ..\TCCSELF.COM -nostdlib -c -I../INCLUDE -o FDSELF.O fdhello.c
    ..\TCCSELF.COM -nostdlib -r -o FDSELF.RLO FDSELF.O ../LIB/FDOSCRT.O -L../LIB -lfdos
    ..\E2ESELF.COM /y --thumb2 -o FDSELF.EXE FDSELF.RLO
    FDSELF.EXE

The expected result is "Native FDOS C: FDSELF.EXE". The scripts keep
TCC.COM and ELF2EZ.COM available for another rebuild.
