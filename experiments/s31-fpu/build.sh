#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: $0 RV32_LINUX_GCC NEW_OUTPUT_DIR" >&2
    exit 2
fi
cc=$(realpath "$1")
prefix=${cc%gcc}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$2"
out=$(realpath "$2")
"$cc" -march=rv32imac -mabi=ilp32 -O2 -g0 -static -Wall -Wextra -Werror \
    -o "$out/fpu-test" "$here/fpu-test.c" "$here/fpu-regs.S"
"${prefix}strip" -o "$out/fpu-test.stripped" "$out/fpu-test"
gzip -n -c "$out/fpu-test.stripped" > "$out/fpu-test.gz"
"${prefix}readelf" -h -A "$out/fpu-test" > "$out/elf.txt"
grep -q 'soft-float ABI' "$out/elf.txt"
(cd "$out" && sha256sum fpu-test.stripped fpu-test.gz > SHA256SUMS)
cat "$out/SHA256SUMS"
