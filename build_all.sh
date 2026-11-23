#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BOARDS=(M1 M2 PC Z2 C2)
EXTRA_ARGS=("$@")
for arg in "${EXTRA_ARGS[@]}"; do
    case "$arg" in
        --emm)
            echo "build_all controls EMM itself and builds both OFF and ON variants; do not pass --emm." >&2
            exit 2 ;;
    esac
done
COUNT=0
TOTAL=$(( ${#BOARDS[@]} * 4 ))

build_one() {
    local board="$1" video="$2" emm="$3" paging="${4:-PAGING}"
    local tag="${board}-386-${video}"
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
        --board "$board" --video "$video" \
        --build-dir "$SCRIPT_DIR/build/all/$tag" \
        "${emm_args[@]}" "${paging_args[@]}" "${EXTRA_ARGS[@]}"
}

for board in "${BOARDS[@]}"; do
    for emm in OFF ON; do
        build_one "$board" RUNTIME "$emm"
        build_one "$board" VGA256 "$emm" NP
    done
done

printf '\nAll %d XRIP 386 variants (with and without EMM) built. UF2 files are under bin/<build-type>/.\n' "$TOTAL"
