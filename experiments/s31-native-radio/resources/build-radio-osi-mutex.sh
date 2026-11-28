#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# PINNED_HEADER_DIR CROSS_PREFIX NEW_OUTPUT_DIR
set -eu
test "$#" = 3
src=$(realpath "$(dirname "$0")")
h=$(realpath "$1")
p=$2
case "$p" in */*) p="$(realpath "$(dirname "$p")")/$(basename "$p")";; esac
test "$(git hash-object "$h/wifi_os_adapter.h")" = 0495a83c5c2a28516e4997a69b9f596de9a86a0c
umask 077
mkdir "$3"
cd "$3"
printf '#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n' > sdkconfig.h
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I. -I"$h" "$src/radio-osi-mutex.c" "$src/test-radio-osi-mutex.c" -o host-test
timeout 15s ./host-test
echo S31_WIFI_MUTEX_HOST_PASS
cc -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -I. -I"$h" "$src/radio-osi-mutex.c" "$src/test-radio-osi-mutex.c" -o host-sanitized
ASAN_OPTIONS=detect_leaks=1 timeout 15s ./host-sanitized
echo S31_WIFI_MUTEX_SANITIZED_PASS
"${p}gcc" -march=rv32imafc -mabi=ilp32f -std=c11 -O2 -Wall -Wextra -Werror -pthread -static -I. -I"$h" "$src/radio-osi-mutex.c" "$src/test-radio-osi-mutex.c" -o test-rv32
"${p}readelf" -h -A test-rv32 > attributes.txt
grep -q 'single-float ABI' attributes.txt
grep -Fq 'Tag_RISCV_arch: "rv32i2p1_m2p0_a2p1_f2p2_c2p0_zicsr2p0_zmmul1p0_zca1p0_zcf1p0"' attributes.txt
sha256sum host-test host-sanitized test-rv32 sdkconfig.h attributes.txt > SHA256SUMS
sha256sum -c SHA256SUMS
echo S31_WIFI_MUTEX_REPRO_PASS
