"""Stage the native compiler and SDK with their original source and header names."""
import shutil
import subprocess
import sys
from pathlib import Path


def stage(root, destination, fdoscrt, wincrt, library, tcc, elf2ez, converter):
    compiler = root / "apps/tcc"
    dos_api = root / "apps/api"
    win = root / "apps/win"
    converter_source = root / "apps/elf2ez/elf2ez.c"
    sources = sorted(compiler.glob("*.c")) + sorted(compiler.glob("*.h")) + sorted(compiler.glob("*.def"))
    headers = {}
    for tree in [dos_api, win / "include", win / "ddi", compiler / "include", compiler / "compat"]:
        for path in sorted(tree.rglob("*.h")):
            headers[path.relative_to(tree).as_posix()] = path

    for folder, inputs in [
        ("SRC", [(path.name, path) for path in [*sources, converter_source]]),
        ("INCLUDE", headers.items()),
    ]:
        for name, path in inputs:
            output = destination / folder / name
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, output)
    lib = destination / "LIB"
    lib.mkdir(exist_ok=True)
    for path, name in [(fdoscrt, "FDOSCRT.O"), (wincrt, "WINCRT.O"), (library, "libfdos.a")]:
        shutil.copyfile(path, lib / name)

    for image, name in [(tcc, "TCC.COM"), (elf2ez, "ELF2EZ.COM")]:
        subprocess.run([str(converter), "/y", "-o", str(destination / name), str(image)], check=True)

    units = ["tcc", "libtcc", "tccpp", "tccgen", "tcctype", "tccstruct", "tccexpr",
             "tccstmt", "tccelf", "tccrun", "tccasm", "arm-thumb-gen", "arm-link", "arm-asm",
             "requirements"]
    compile_rsp = ["-nostdinc", "-nostdlib", "-fno-common", "-DNO_SEMAPHORE_H=1",
                   "-DONE_SOURCE=0", "-I.", "-I../INCLUDE", "-c"]
    compile_rsp += [unit + ".c" for unit in units]
    link_rsp = ["-nostdlib", "-r", "-o", "TCCSELF.O", "../LIB/FDOSCRT.O"]
    link_rsp += [unit + ".o" for unit in units]
    link_rsp += ["-L../LIB", "-lfdos"]
    for name, args in [("COMPILE.RSP", compile_rsp), ("LINK.RSP", link_rsp)]:
        (destination / "SRC" / name).write_text("\n".join(args) + "\n", encoding="ascii", newline="\r\n")

    (destination / "SRC/SELF.BAT").write_text(
        "@echo off\necho Compiling TinyCC...\n..\\TCC -v @COMPILE.RSP\nif errorlevel 1 goto failed\n"
        "echo Linking TinyCC...\n..\\TCC @LINK.RSP\nif errorlevel 1 goto failed\n"
        "echo Converting TinyCC to EZ...\n..\\ELF2EZ /y --thumb2 -o ..\\TCCSELF.COM TCCSELF.O\n"
        "if errorlevel 1 goto failed\necho Built ..\\TCCSELF.COM\ngoto done\n"
        ":failed\necho TinyCC self-build failed.\n:done\n",
        encoding="ascii", newline="\r\n")
    (destination / "SRC/MKELF2EZ.BAT").write_text(
        "@echo off\n..\\TCC -nostdlib -fno-common -r -I../INCLUDE -o E2ESELF.O "
        "elf2ez.c ../LIB/FDOSCRT.O -L../LIB -lfdos\n"
        "if errorlevel 1 goto done\n"
        "..\\ELF2EZ /y --thumb2 -o ..\\E2ESELF.COM E2ESELF.O\n:done\n",
        encoding="ascii", newline="\r\n")
    for sample in sorted((compiler / "examples").glob("*.c")):
        shutil.copyfile(sample, destination / "SRC" / sample.name)
    check = (compiler / "examples/CHECK.BAT").read_text(encoding="ascii")
    (destination / "SRC/CHECK.BAT").write_text(check, encoding="ascii", newline="\r\n")
    shutil.copyfile(compiler / "COPYING", destination / "COPYING")


if __name__ == "__main__":
    stage(*map(Path, sys.argv[1:]))
