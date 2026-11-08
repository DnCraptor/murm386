#!/usr/bin/env bash
# Rebuild everything of the native Windows: WINN.EXE (apps/win), the driver
# modules (apps/windrv/*) and the applications (apps/winapps/*), each in a
# fresh build directory, into apps/compiled. Mirrors build_win.bat.
#
# The same sources give byte-identical files only with the same toolchain,
# Pico SDK and build type. This script fixes the build type (MinSizeRel) and
# the generator (Ninja); the SDK and the toolchain are the ones the
# CMakeLists name for the Pico VS Code extension (~/.pico-sdk: SDK 2.2.0,
# Arm GNU Toolchain 14_2_Rel1), or PICO_SDK_PATH / PICO_TOOLCHAIN_PATH from
# the environment. At the end it prints the compiler and a SHA-256 of every
# file, to compare builds.
#
#   apps/win/build_win.sh [-j N]
set -euo pipefail

WIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$WIN_DIR/../.." && pwd)"
BUILD_ROOT="$ROOT/build/win"
COMPILED="$ROOT/apps/compiled"
BUILD_TYPE="MinSizeRel"
JOBS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs) JOBS=(--parallel "$2"); shift 2 ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

# the projects, in build order (the core first: it owns the export tables)
PROJECTS=("$WIN_DIR")
for d in "$ROOT"/apps/windrv/*/ "$ROOT"/apps/winapps/*/; do
    [[ -f "$d/CMakeLists.txt" ]] && PROJECTS+=("${d%/}")
done

# elf2ez turns the linked modules into EZ files
if [[ ! -x "$ROOT/tools/elf2ez" ]]; then
    echo "== elf2ez (host tool)"
    (cd "$ROOT/apps/elf2ez" && ./build-host.sh)
fi

GEN=(-G Ninja)
command -v ninja >/dev/null 2>&1 || GEN=()
SDK_ARGS=()
[[ -n "${PICO_SDK_PATH:-}" ]] && SDK_ARGS+=("-DPICO_SDK_PATH=$PICO_SDK_PATH")
[[ -n "${PICO_TOOLCHAIN_PATH:-}" ]] && SDK_ARGS+=("-DPICO_TOOLCHAIN_PATH=$PICO_TOOLCHAIN_PATH")

OUTPUTS=()
for p in "${PROJECTS[@]}"; do
    name="$(basename "$p")"
    dir="$BUILD_ROOT/$name"
    echo
    echo "== $name"
    rm -rf "$dir"
    mkdir -p "$BUILD_ROOT"
    cmake "${GEN[@]}" -S "$p" -B "$dir" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "${SDK_ARGS[@]}" >"$dir.log" 2>&1 ||
        { cat "$dir.log"; echo "configure failed: $name" >&2; exit 1; }
    cmake --build "$dir" "${JOBS[@]}" 2>&1 | tee -a "$dir.log" | grep -E "EZ image|warning|error" || true
    [[ ${PIPESTATUS[0]} -eq 0 ]] || { echo "build failed: $name (see $dir.log)" >&2; exit 1; }
    # the EZ file this project wrote: the newest file in apps/compiled
    out="$(ls -t "$COMPILED" | head -n 1)"
    OUTPUTS+=("$out")
done

CC="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$BUILD_ROOT/$(basename "$WIN_DIR")/CMakeCache.txt")"
SDK="$(sed -n 's/^PICO_SDK_PATH:[A-Z]*=//p' "$BUILD_ROOT/$(basename "$WIN_DIR")/CMakeCache.txt")"
SDKV="$(sed -n 's/^ *set(PICO_SDK_VERSION_\(MAJOR\|MINOR\|REVISION\) \([0-9]*\)).*/\2/p' "$SDK/pico_sdk_version.cmake" | paste -sd.) ($SDK)"
echo
echo "Compiler : $("$CC" --version | head -n 1)"
echo "Pico SDK : $SDKV"
echo "Type     : $BUILD_TYPE"
echo
cd "$COMPILED"
for f in "${OUTPUTS[@]}"; do
    printf '%-12s %8s  %s\n' "$f" "$(stat -c %s "$f")" "$(sha256sum "$f" | cut -d' ' -f1)"
done
