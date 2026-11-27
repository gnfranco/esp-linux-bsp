#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Usage: build-radio-sram-backend.sh PINNED_HEADER_DIR NEW_OUTPUT_DIR
set -eu
test "$#" -eq 2
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
d=$(CDPATH= cd -- "$1" && pwd)
inc="$d/components/bt/porting_btdm/controller/btdm_common/include"
test "$(git hash-object "$d/wifi_os_adapter.h")" = 0495a83c5c2a28516e4997a69b9f596de9a86a0c
test "$(git hash-object "$d/esp_coexist_adapter.h")" = 15e8ef7114fefeba8671c51a42f0e60ee9de0ec8
test "$(git hash-object "$inc/btdm_osal.h")" = 1975300b49b1a921eb29e6b2b2d9b79740cdb319
mkdir "$2"
o=$(CDPATH= cd -- "$2" && pwd)
cat > "$o/sdkconfig.h" <<'CONFIG'
#define CONFIG_IDF_TARGET_ESP32S31 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_BT_CTRL_MULTI_LINK_ENABLED 1
CONFIG
cd "$src"
set -- radio-ledger.c radio-alloc.c radio-osi-alloc.c radio-sram-backend.c test-radio-sram-backend.c
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I"$o" -I"$d" -I"$inc" "$@" -o "$o/test-host"
"$o/test-host" host
cc -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -I"$o" -I"$d" -I"$inc" "$@" -o "$o/test-asan"
ASAN_OPTIONS=detect_leaks=1 "$o/test-asan" host
cross="$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-"
"${cross}gcc" -std=c11 -O2 -Wall -Wextra -Werror -pthread -march=rv32imac -mabi=ilp32 -static -I"$o" -I"$d" -I"$inc" "$@" -o "$o/test-rv32"
"${cross}readelf" -A "$o/test-rv32"
cd "$o"
gzip -n -c test-rv32 > test-rv32.gz
sha256sum test-host test-asan test-rv32 test-rv32.gz sdkconfig.h > SHA256SUMS
sha256sum -c SHA256SUMS
cat SHA256SUMS
