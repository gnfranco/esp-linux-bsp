#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Source-only registration probe; do not redistribute linked vendor archive.
set -eu
if [ "$#" -ne 4 ]; then
    echo "usage: $0 DEPS_DIR RV32_GCC PINNED_ADAPTER_HEADER NEW_OUTPUT_DIR" >&2
    exit 2
fi
deps=$(realpath "$1")
cc=$(realpath "$2")
header=$(realpath "$3")
out=$4
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${cc%gcc}
python3 "$here/audit.py" "$deps" "$cc" --verify-only
# Espressif ESP-IDF v6.1: components/esp_coex/include/private/esp_coexist_adapter.h
test "$(basename "$header")" = esp_coexist_adapter.h
test "$(git hash-object "$header")" = 15e8ef7114fefeba8671c51a42f0e60ee9de0ec8
mkdir "$out"
out=$(realpath "$out")
"$cc" -march=rv32imafc -mabi=ilp32f -Os -ffreestanding -fno-builtin \
    -fno-pic -fno-pie -msmall-data-limit=0 -mno-relax \
    -fno-stack-protector -ffunction-sections -fdata-sections \
    -Wall -Wextra -Werror -nostdlib -static -no-pie \
    -I "$(dirname "$header")" \
    -Wl,--gc-sections,--no-relax,--no-undefined,-e,_start \
    -Wl,-Map,"$out/coex_register.map" -o "$out/coex_register.elf" \
    "$here/coex-register.c" "$deps/esp-coex-lib/esp32s31/libcoexist.a"
"${prefix}readelf" -h -l -A "$out/coex_register.elf" > "$out/elf.txt"
"${prefix}objdump" -d "$out/coex_register.elf" > "$out/disassembly.txt"
"${prefix}nm" -u "$out/coex_register.elf" > "$out/undefined.txt"
"${prefix}nm" -g --defined-only "$out/coex_register.elf" > "$out/defined.txt"
test ! -s "$out/undefined.txt"
grep -q 'ELF32' "$out/elf.txt"
grep -q 'RISC-V' "$out/elf.txt"
grep -q 'single-float ABI' "$out/elf.txt"
! grep -q 'INTERP' "$out/elf.txt"
grep -q '<esp_coex_adapter_register>:' "$out/disassembly.txt"
! grep -Eq '^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]+[[:space:]]+(f[^[:space:]]*|c[.]f[^[:space:]]*)' "$out/disassembly.txt"
! grep -Eq ' (coex_init|coex_core_init|coex_enable|coex_pre_init|coex_hw_timer_enable)$' "$out/defined.txt"
(cd "$out" && sha256sum coex_register.elf > SHA256SUMS)
echo S31_COEX_REGISTER_BUILD_PASS
