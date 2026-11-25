#!/usr/bin/env bash
# Differential test of src/fdos/xlat against an XRIP-equivalent interpreter.
# Linux/WSL: needs gcc-arm-linux-gnueabihf and qemu-user (or an ARM host).
#
#   tools/xlat_test/run.sh [programs] [first-seed]
#
# XLAT_LOCKSTEP=1  check the state after every translated run (slower)
# XLAT_SMC=1       make every program write into its own code
# First: the shift helper against the reference for all counts (--shift).
set -euo pipefail
cd "$(dirname "$0")/../.."
out="${TMPDIR:-/tmp}/xlat_test"
arm-linux-gnueabihf-gcc -O2 -g -mthumb -march=armv7-a+fp -static -Wall -Wextra \
    -DXLAT_HOST_TEST -Isrc/fdos/xlat \
    tools/xlat_test/xlat_test.c src/fdos/xlat/*.c -o "$out"
run=()
[[ "$(uname -m)" == arm* || "$(uname -m)" == aarch64 ]] || run=(qemu-arm)
"${run[@]}" "$out" --shift
"${run[@]}" "$out" "${1:-5000}" "${2:-1}"
