#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Link-only test; do not execute or distribute vendor objects.
set -eu
export LC_ALL=C
test "$#" = 4 || { echo "usage: $0 SOURCE_ROOT CLOSURE_OBJECT CROSS_PREFIX NEW_OUTPUT_DIR" >&2; exit 2; }
src=$(realpath "$1")
closure=$(realpath "$2")
p=$3
case "$p" in
    */*) p="$(realpath "$(dirname "$p")")/$(basename "$p")" ;;
esac
out=$4
h="$src/components/esp_wifi/include/esp_private/ftm_calibration_data.h"
c="$src/components/esp_wifi/src/ftm_load_calibration.c"
test "$(git hash-object "$h")" = be457ad920d675adaefe36f8c8e190a3895065d8
test "$(git hash-object "$c")" = 8e937843d78279de4b28e6eabc8933fd4b6d50ab
printf '%s  %s\n' adec29146d41273e904a9f5b420baed911b2243b8d5b3331ce177720a52e5b22 "$closure" | sha256sum -c -
umask 077
mkdir "$out"
cd "$out"
printf '#define CONFIG_ESP_WIFI_FTM_ENABLE 1\n#define CONFIG_IDF_TARGET_ESP32S31 1\n' > sdkconfig.h
"${p}gcc" -march=rv32imafc -mabi=ilp32f -ffreestanding -fdata-sections -Wall -Wextra -Werror -I. -I"$src/components/esp_wifi/include" -c "$c" -o ftm.o
"${p}readelf" -h ftm.o | grep 'single-float ABI'
"${p}nm" -g --defined-only ftm.o | awk '$2=="D" {print $3}' | sort -u > definitions.txt
"${p}ld" -m elf32lriscv -r "$closure" -o control.o
"${p}ld" -m elf32lriscv -r "$closure" ftm.o -o wifi-ftm-closure.o
for f in control wifi-ftm-closure; do
    "${p}nm" -u "$f.o" | awk '{print $NF}' | sort -u > "$f.txt"
done
comm -23 control.txt wifi-ftm-closure.txt > resolved.txt
comm -13 control.txt wifi-ftm-closure.txt > introduced.txt
test "$(wc -l < control.txt)" = 217
test "$(wc -l < wifi-ftm-closure.txt)" = 193
test "$(wc -l < resolved.txt)" = 24
test ! -s introduced.txt
cmp resolved.txt definitions.txt
test "$(grep -c '^est_PHY_.*FTM_COMP_' resolved.txt)" = 24
sha256sum sdkconfig.h ftm.o control.o wifi-ftm-closure.o control.txt wifi-ftm-closure.txt resolved.txt definitions.txt introduced.txt > SHA256SUMS
sha256sum -c SHA256SUMS
echo S31_WIFI_FTM_LINK_PROOF_PASS
