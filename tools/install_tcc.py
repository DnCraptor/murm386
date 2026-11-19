"""Install the built Murm386 TinyCC SDK on a mounted FDOS volume."""

import argparse
import shutil
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", required=True, type=Path, help="mounted FDOS volume root")
    parser.add_argument("--sdk", type=Path, default=root / "apps/tcc/build/TCC",
                        help="staged TCC directory (default: apps/tcc/build/TCC)")
    args = parser.parse_args()

    sdk = args.sdk.resolve()
    volume = args.dest.resolve()
    if not volume.is_dir():
        parser.error(f"destination directory does not exist: {volume}")
    target = (volume / "TCC").resolve()
    if sdk == target or sdk in target.parents or target in sdk.parents:
        parser.error("source and destination SDK directories overlap")

    required = (
        "TCC.COM", "ELF2EZ.COM", "LIB/FDOSCRT.O", "LIB/WINCRT.O", "LIB/libfdos.a",
        "INCLUDE/stdio.h", "INCLUDE/windows.h", "SRC/fdhello.c", "SRC/wnhello.c",
        "SRC/CHECK.BAT", "SRC/SELF.BAT", "SRC/MKELF2EZ.BAT",
    )
    missing = [name for name in required if not (sdk / name).is_file()]
    if missing:
        parser.error(f"incomplete TinyCC SDK at {sdk}; build tcc_sdk first. Missing: "
                     + ", ".join(missing))

    shutil.copytree(sdk, target, dirs_exist_ok=True, copy_function=shutil.copyfile)
    print(f"Installed TinyCC SDK at {target}")


if __name__ == "__main__":
    main()
