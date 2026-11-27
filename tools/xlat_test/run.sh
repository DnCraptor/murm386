#!/usr/bin/env bash
# Differential test of src/fdos/xlat against an XRIP-equivalent interpreter.
# Linux/WSL: needs gcc-arm-linux-gnueabihf and qemu-user (or an ARM host).
#
#   tools/xlat_test/run.sh [programs] [first-seed]
#
# XLAT_LOCKSTEP=1  check the state after every translated run (slower)
# XLAT_SMC=1       make every program write into its own code
# XLAT_KEEP=1      no cache flush between programs (the ring cache evicts)
# First: the shift and MUL/DIV helpers against the reference (--shift), then
# hand-made regions (regions.log: REP CMPS/SCAS, shifts, CALL/RET, MUL/DIV, SP,
# segment registers, far and indirect transfers, PUSHA/POPA, ENTER/LEAVE, flags,
# x87). The x87 is murm386's fpu.c (tools/xlat_test/fpu_glue.c).
set -euo pipefail
cd "$(dirname "$0")/../.."
out="${TMPDIR:-/tmp}/xlat_test"
arm-linux-gnueabihf-gcc -O2 -g -mthumb -march=armv7-a+fp -static -Wall -Wextra \
    -DXLAT_HOST_TEST -DXLAT_REGION_INSNS=16 -Isrc/fdos/xlat -Isrc \
    tools/xlat_test/xlat_test.c tools/xlat_test/fpu_glue.c src/fpu.c src/fdos/xlat/*.c \
    -lm -o "$out"
run=()
[[ "$(uname -m)" == arm* || "$(uname -m)" == aarch64 ]] || run=(qemu-arm)
"${run[@]}" "$out" --compile-adaptive
"${run[@]}" "$out" --compile-resume
"${run[@]}" "$out" --compile-scheduler
"${run[@]}" "$out" --shift   # shift and MUL/DIV helpers
"${run[@]}" "$out" --replay tools/xlat_test/regions.log 300   # hand-made regions
"${run[@]}" "$out" "${1:-5000}" "${2:-1}"
# ring cache: a small arena and 4 region slots evict on every translation;
# no flush between programs (XLAT_KEEP)
arm-linux-gnueabihf-gcc -O2 -g -mthumb -march=armv7-a+fp -static -Wall -Wextra \
    -DXLAT_HOST_TEST -DXLAT_ARENA_SIZE=6144 -DXLAT_MAX_REGIONS=4 \
    -Isrc/fdos/xlat -Isrc \
    tools/xlat_test/xlat_test.c tools/xlat_test/fpu_glue.c src/fpu.c src/fdos/xlat/*.c \
    -lm -o "$out-ring"
XLAT_KEEP=1 "${run[@]}" "$out-ring" "${1:-5000}" "${2:-1}"
# ring only: 64 slots in a small arena, the ring head evicts what it needs
arm-linux-gnueabihf-gcc -O2 -g -mthumb -march=armv7-a+fp -static -Wall -Wextra \
    -DXLAT_HOST_TEST -DXLAT_ARENA_SIZE=12288 \
    -Isrc/fdos/xlat -Isrc \
    tools/xlat_test/xlat_test.c tools/xlat_test/fpu_glue.c src/fpu.c src/fdos/xlat/*.c \
    -lm -o "$out-ring2"
XLAT_KEEP=1 "${run[@]}" "$out-ring2" "${1:-5000}" "${2:-1}"
