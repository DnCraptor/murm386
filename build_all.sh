#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CPU_TARGETS=(286 386)
if [[ $# -gt 0 && "$1" != -* ]]; then
    case "${1,,}" in
        286|386) CPU_TARGETS=("${1,,}") ;;
        all) ;;
        *) echo "Invalid CPU target '$1'. Use 286, 386 or all." >&2; exit 2 ;;
    esac
    shift
fi

BOARDS=(M1 M2 PC Z2 C2)
EXTRA_ARGS=("$@")
for arg in "${EXTRA_ARGS[@]}"; do
    case "$arg" in
        --emm)
            echo "build_all controls EMM itself and builds both OFF and ON variants; do not pass --emm." >&2
            exit 2 ;;
        --cpu|-286|-386)
            echo "Select the CPU target with the first positional argument: 286, 386 or all." >&2
            exit 2 ;;
    esac
done
COUNT=0
TOTAL=$(( ${#CPU_TARGETS[@]} * ${#BOARDS[@]} * 4 ))

build_one() {
    local cpu="$1" board="$2" video="$3" emm="$4" paging="${5:-PAGING}"
    local tag="${board}-${cpu}-${video}"
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
        --cpu "$cpu" --board "$board" --video "$video" \
        --build-dir "$SCRIPT_DIR/build/all/$tag" \
        "${emm_args[@]}" "${paging_args[@]}" "${EXTRA_ARGS[@]}"
}

for cpu in "${CPU_TARGETS[@]}"; do
    for board in "${BOARDS[@]}"; do
        for emm in OFF ON; do
            build_one "$cpu" "$board" RUNTIME "$emm"
            build_one "$cpu" "$board" VGA256 "$emm" NP
        done
    done
done

printf '\nAll %d requested CPU variants (with and without EMM) built. UF2 files are under bin/<build-type>/.\n' "$TOTAL"
