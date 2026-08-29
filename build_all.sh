#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CPU_TARGET="286"
if [[ $# -gt 0 && "$1" != -* ]]; then
    CPU_TARGET="$1"
    shift
fi

if [[ "$CPU_TARGET" != "286" ]]; then
    echo "CPU target '$CPU_TARGET' is not enabled in build_all: the 386 branch is currently untested." >&2
    exit 2
fi

BOARDS=(M1 M2 PC Z2 C2)
EXTRA_ARGS=("$@")
for arg in "${EXTRA_ARGS[@]}"; do
    if [[ "$arg" == "--emm" ]]; then
        echo "build_all controls EMM itself and builds both OFF and ON variants; do not pass --emm." >&2
        exit 2
    fi
done
COUNT=0
TOTAL=32

build_one() {
    local board="$1" video="$2" audio="$3" emm="$4" paging="${5:-PAGING}"
    local tag="${board}-${CPU_TARGET}-${video}-${audio}"
    local emm_args=() paging_args=()
    if [[ "$emm" == "ON" ]]; then
        tag+="-emm"
        emm_args+=(--emm)
    fi
    if [[ "$paging" == "NP" ]]; then
        tag+="-np"
        paging_args+=(--no-paging)
    fi
    COUNT=$((COUNT + 1))
    printf '\n[%d/%d] %s\n' "$COUNT" "$TOTAL" "$tag"
    "$SCRIPT_DIR/build.sh" \
        --board "$board" --video "$video" --audio "$audio" \
        --build-dir "$SCRIPT_DIR/build/all/$tag" \
        "${emm_args[@]}" "${paging_args[@]}" "${EXTRA_ARGS[@]}"
}

for board in "${BOARDS[@]}"; do
    case "$board" in
        PC) audios=(PWM) ;;
        C2) audios=(I2S) ;;
        *) audios=(I2S PWM) ;;
    esac
    for audio in "${audios[@]}"; do
        for emm in OFF ON; do
            # Common paging firmware: MCGA/EGA128/EGA256/VGA128/VGA256 selected at runtime.
            build_one "$board" RUNTIME "$audio" "$emm"
            # The only separate memory model: VGA256 with direct QSPI guest RAM.
            build_one "$board" VGA256 "$audio" "$emm" NP
        done
    done
done

printf '\nAll %d supported 286 variants (with and without EMM) built. UF2 files are under bin/<build-type>/.\n' "$TOTAL"
