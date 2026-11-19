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
cmake -S apps/tcc -B apps/tcc/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/tcc/build --target tcc_sdk
```

On Windows, the host build uses MinGW `gcc.exe` from PATH. Set
`ELF2EZ_HOST_GCC` to the full path when MinGW is outside PATH.
With a Ninja build, CMake uses the same Ninja executable for the host tool.
`ELF2EZ_HOST_TOOL` can point at a separately built host converter with
`-o` support.

The build also writes `apps/compiled/tcc.com` and
`apps/compiled/elf2ez.com`.

## Install on a mounted volume

Run this from the repository root. Use the host path of the mounted FAT
volume that FDOS will see. For an image-backed DOS drive, use the mount
point of the FAT partition inside the image.

```sh
python tools/install_tcc.py --dest E:/
```

The installer copies the staged SDK into `TCC` at the volume root. It
replaces files present in the staged SDK and leaves other files in place.
Use `--sdk path/to/build/TCC` with a different CMake build directory.
The installed `\TCC\README.TXT` covers the SDK layout, FDOS and WINN
checks on the target, and rebuilding TinyCC and ELF2EZ in Native FreeDOS.
On the installed drive, open it with `TYPE \TCC\README.TXT`.
See [the target README](README.TXT) for the same instructions in the source tree.
