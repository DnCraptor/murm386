# Native TinyCC for Murm386

The compiler is an ARM FDOS application like Doom. The bootstrap uses the
Murmulator OS2 TinyCC port. It also carries the PicoMite Thumb VFP-register copy
fix, read-only constants, and heap-backed compiler value stack. Programs
produced by TinyCC use Thumb-2 and the softfp calling convention and require
Murm386 on RP2350. Building with GCC can still produce Thumb-1 EZ images.

## Bootstrap

Build with the Pico SDK, ARM GCC and a host C compiler. CMake builds the
host converter from the same checkout in the build directory.

```sh
cmake -S apps/tcc -B build/tcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/tcc --target tcc_sdk
```

On Windows, the host build uses MinGW `gcc.exe` from PATH. Set
`ELF2EZ_HOST_GCC` to the full path when MinGW is outside PATH.
With a Ninja build, CMake uses the same Ninja executable for the host tool.
`ELF2EZ_HOST_TOOL` can point at a separately built host converter with
`-o` support.

The build also writes `apps/compiled/tcc.com` and
`apps/compiled/elf2ez.com`.

Copy the generated `build/tcc/TCC` directory to the root of the FDOS
drive. It contains `TCC.COM`, `ELF2EZ.COM`, headers, compiler and converter
sources, and the GCC-built ARM startup objects and `libfdos.a`. The staged
names match the originals. The SDK uses the FDOS long filename calls for
opening, creating, checking and deleting files on FAT volumes.
Use the same drive for source files or pass explicit include and library
paths. Systems without sufficient conventional memory need available
application PSRAM for compiler allocations and converter workspace.

## Build on FDOS

From the installed `\TCC\SRC` directory, use the staged samples.

```bat
..\TCC -nostdlib -r -I../INCLUDE -o FDHELLO.O FDHELLO.C ../LIB/FDOSCRT.O -L../LIB -lfdos
..\ELF2EZ /y --thumb2 FDHELLO.O
FDHELLO.EXE
```

The compiler can compile individual files with `-c` and then combine those
objects with `-r`. Put libraries after the objects that need them.
`elf2ez` consumes little-endian ARM ET_REL input with a global Thumb
`__ez_start`. It produces an EZ executable, normally named after the input
with an `.EXE` extension. `-o` selects an explicit filename.
Always pass `--thumb2` for TinyCC output, including code whose instructions
require Thumb-2 but whose ELF relocations alone do not establish that.

## Build for WINN

Use `WINCRT.O` in place of `FDOSCRT.O`. It contains the existing WINN
startup and import stubs with generic application metadata. Start the
produced executable from WINN.

```bat
..\TCC -nostdlib -r -I../INCLUDE -o WNHELLO.O WNHELLO.C ../LIB/WINCRT.O -L../LIB -lfdos
..\ELF2EZ /y --thumb2 WNHELLO.O
```

## Self-host

From `\TCC\SRC`, run `SELF.BAT` to build `\TCC\TCCSELF.COM` using
TinyCC. The response files compile the compiler translation units
separately, so their temporary storage can be released before the next
unit. `MKELF2EZ.BAT` builds `\TCC\E2ESELF.COM` from the same converter
source used by both bootstrap builds. Both scripts reuse the installed
startup objects and runtime archive. Keep the bootstrap commands until
the rebuilt versions have been tested on the board.

The usual FDOS path and file constraints apply. The supported output path
is `-c` or `-r` followed by `elf2ez`. The native port does not provide a
dynamic linker or a `-run` mode.
