#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 DEPS_DIR RV32_GCC NEW_OUTPUT_DIR" >&2
    exit 2
fi
deps=$(realpath "$1")
cc=$(realpath "$2")
out=$3
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${cc%gcc}
python3 "$here/audit.py" "$deps" "$cc" --verify-only
mkdir "$out"
out=$(realpath "$out")
coex="$deps/esp-coex-lib/esp32s31/libcoexist.a"
bt="$deps/esp32s31-bt-lib/libbtdm_common.a"
phy="$deps/esp-phy-lib/esp32s31/libphy.a"
"$cc" -march=rv32imafc -mabi=ilp32f -Os -ffreestanding -fno-builtin \
    -fno-pic -fno-pie -msmall-data-limit=0 -mno-relax \
    -fno-stack-protector -ffunction-sections -fdata-sections \
    -Wall -Wextra -Werror -nostdlib -static -no-pie \
    -Wl,--gc-sections,--no-relax,--no-undefined,-e,_start \
    -Wl,-Map,"$out/abi_probe.map" -o "$out/abi_probe.elf" \
    "$here/abi_probe.c" "$coex" "$bt" "$phy"
"${prefix}readelf" -h -l -A "$out/abi_probe.elf" > "$out/elf.txt"
"${prefix}objdump" -d "$out/abi_probe.elf" > "$out/disassembly.txt"
"${prefix}nm" -u "$out/abi_probe.elf" > "$out/undefined.txt"
test ! -s "$out/undefined.txt"
# A metadata-only probe must not execute any recognized FP instruction.
if grep -Eq '^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]+[[:space:]]+(f[^[:space:]]*|c[.]f[^[:space:]]*)' "$out/disassembly.txt"; then
    echo "Unexpected floating-point opcode; refusing metadata probe" >&2
    exit 1
fi
grep -q 'ELF32' "$out/elf.txt"
grep -q 'RISC-V' "$out/elf.txt"
grep -q 'single-float ABI' "$out/elf.txt"
! grep -q 'INTERP' "$out/elf.txt"
for sym in coex_version_get r_btdm_get_compile_version phy_get_rf_cal_version; do
    grep -q "<$sym>:" "$out/disassembly.txt"
done
(cd "$out" && sha256sum abi_probe.elf > SHA256SUMS)
echo "S31_RADIO_ABI_BUILD_PASS"
