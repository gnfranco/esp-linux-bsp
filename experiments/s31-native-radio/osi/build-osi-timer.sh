#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Host tests and ESP GCC 14.1.1 RV32IMAC ILP32 static target, no vendor libs.
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: $0 RV32_GCC NEW_OUTPUT_DIR" >&2
    exit 2
fi
cc=$(realpath "$1")
out=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${cc%gcc}
test "$("$cc" -dumpfullversion)" = 14.1.1
mkdir "$out"
out=$(realpath "$out")
flags="-std=c11 -Wall -Wextra -Werror -pthread"
${HOST_CC:-cc} $flags -O2 "$here/osi-sem.c" "$here/osi-timer.c" "$here/test-osi-timer.c" -o "$out/test-host"
${HOST_CC:-cc} $flags -O1 -g -fno-omit-frame-pointer -fno-pie -no-pie \
    -DS31_TEST_LARGE_STACK -fsanitize=address,undefined \
    "$here/osi-sem.c" "$here/osi-timer.c" "$here/test-osi-timer.c" -o "$out/test-host-asan"
"$cc" $flags -Os -march=rv32imac -mabi=ilp32 -static -s \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    "$here/osi-sem.c" "$here/osi-timer.c" "$here/test-osi-timer.c" -o "$out/test-rv32"
"${prefix}readelf" -h -l -A "$out/test-rv32" > "$out/rv32-elf.txt"
grep -q 'ELF32' "$out/rv32-elf.txt"
grep -q 'RISC-V' "$out/rv32-elf.txt"
grep -q 'soft-float ABI' "$out/rv32-elf.txt"
if grep -q 'INTERP' "$out/rv32-elf.txt"; then
    echo "unexpected ELF interpreter" >&2; exit 1
fi
grep -q 'Tag_RISCV_arch:' "$out/rv32-elf.txt"
if grep 'Tag_RISCV_arch:' "$out/rv32-elf.txt" | grep -Eq '_(f|d|b)[0-9]|_z(b[abcs]|cf)[0-9]'; then
    echo "unexpected ISA attribute from linked runtime" >&2; exit 1
fi
"${prefix}objdump" -d "$out/test-rv32" > "$out/rv32-disassembly.txt"
if grep -E '^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]+[[:space:]]+(f[^[:space:]]*|c[.]f[^[:space:]]*)' "$out/rv32-disassembly.txt" |
        grep -Ev '[[:space:]]fence([.]|[[:space:]]|$)' | grep -q .; then
    echo "unexpected FP opcode" >&2; exit 1
fi
(cd "$out" && sha256sum test-host test-host-asan test-rv32 > SHA256SUMS)
stat -c '%n %s bytes' "$out/test-host" "$out/test-host-asan" "$out/test-rv32"
echo S31_OSI_TIMER_BUILD_PASS
