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
    -o "$out/fpu-extended" "$here/fpu-extended.c" "$here/fpu-regs.S" "$here/fpu-extra.S"
"$cc" -march=rv32imafc -mabi=ilp32f -O2 -g0 -ffreestanding -fno-builtin \
    -ffp-contract=off -fno-pic -fno-pie -msmall-data-limit=0 -mno-relax \
    -fno-stack-protector -Wall -Wextra -Werror -nostdlib -static -no-pie \
    -Wl,--no-relax,--no-undefined,-e,_start -o "$out/abi-hardfloat.elf" \
    "$here/abi-hardfloat.c" "$here/abi-hardfloat-func.c"
"${prefix}readelf" -h -A -l "$out/fpu-extended" > "$out/extended-elf.txt"
"${prefix}readelf" -h -A -l "$out/abi-hardfloat.elf" > "$out/hardfloat-elf.txt"
grep -q "soft-float ABI" "$out/extended-elf.txt"
grep -q "single-float ABI" "$out/hardfloat-elf.txt"
! grep -q "INTERP" "$out/hardfloat-elf.txt"
"${prefix}objdump" -d "$out/abi-hardfloat.elf" > "$out/hardfloat-dis.txt"
grep -q 'fmul.s' "$out/hardfloat-dis.txt"
grep -q 'flw.*0(sp)' "$out/hardfloat-dis.txt"
"${prefix}strip" -o "$out/fpu-extended.stripped" "$out/fpu-extended"
"${prefix}strip" -o "$out/abi-hardfloat.stripped" "$out/abi-hardfloat.elf"
gzip -n -c "$out/fpu-extended.stripped" > "$out/fpu-extended.gz"
gzip -n -c "$out/abi-hardfloat.stripped" > "$out/abi-hardfloat.gz"
(cd "$out" && sha256sum *.stripped *.gz > SHA256SUMS)
cat "$out/SHA256SUMS"
