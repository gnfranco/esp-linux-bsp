#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Link-only experiment on a private, exact radio closure. Never execute it.
set -eu
export LC_ALL=C
test "$#" = 4 || { echo "usage: $0 PINNED_HEADER_DIR CLOSURE CROSS_PREFIX NEW_OUTPUT_DIR" >&2; exit 2; }
src=$(realpath "$(dirname "$0")/resources")
h=$(realpath "$1")
closure=$(realpath "$2")
p=$3
case "$p" in */*) p="$(realpath "$(dirname "$p")")/$(basename "$p")" ;; esac
out=$4
bt="$h/components/bt/porting_btdm/controller/btdm_common/include"
test "$(git hash-object "$h/wifi_os_adapter.h")" = 0495a83c5c2a28516e4997a69b9f596de9a86a0c
test "$(git hash-object "$h/esp_coexist_adapter.h")" = 15e8ef7114fefeba8671c51a42f0e60ee9de0ec8
test "$(git hash-object "$bt/btdm_osal.h")" = 1975300b49b1a921eb29e6b2b2d9b79740cdb319
printf '%s  %s\n' 3b9aadcc32048d05525d5366558bef68ee0408f92dcd673db4b61ee30257683a "$closure" | sha256sum -c -
umask 077
mkdir "$out"
cd "$out"
printf '#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_BT_CTRL_MULTI_LINK_ENABLED 1\n' > sdkconfig.h
cat > offset-gate.c <<'C'
#include "wifi_os_adapter.h"
_Static_assert(sizeof(void*) == 4, "RV32");
_Static_assert(ESP_WIFI_OS_ADAPTER_VERSION == 9, "OSI version");
_Static_assert(sizeof(wifi_osi_funcs_t) == 512, "OSI size");
_Static_assert(offsetof(wifi_osi_funcs_t, _wifi_malloc) == 360, "callsite");
_Static_assert(offsetof(wifi_osi_funcs_t, _free) == 176, "free slot");
C
"${p}gcc" -march=rv32imafc -mabi=ilp32f -Wall -Wextra -Werror -I. -I"$h" -c offset-gate.c -o offset-gate.o
"${p}readelf" -rW "$closure" > before-relocs.txt
"${p}objcopy" --redefine-sym free=radio_osi_free "$closure" scoped-free.o
"${p}readelf" -rW scoped-free.o > scoped-relocs.txt
"${p}nm" -u scoped-free.o | awk '{print $NF}' | sort -u > scoped-undefined.txt
for f in radio-ledger radio-alloc radio-osi-alloc radio-sram-backend; do
    "${p}gcc" -march=rv32imafc -mabi=ilp32f -std=c11 -O2 -Wall -Wextra -Werror -pthread -ffunction-sections -fdata-sections -I. -I"$h" -I"$bt" -I"$src" -c "$src/$f.c" -o "$f.o"
done
"${p}ld" -m elf32lriscv -r scoped-free.o radio-ledger.o radio-alloc.o radio-osi-alloc.o radio-sram-backend.o -o scoped-osi-closure.o
"${p}readelf" -rW scoped-osi-closure.o > combined-relocs.txt
"${p}nm" -u scoped-osi-closure.o | awk '{print $NF}' | sort -u > combined-undefined.txt
"${p}nm" -g --defined-only scoped-osi-closure.o | grep -E ' T radio_osi_free$'
"${p}readelf" -h -A scoped-osi-closure.o > attributes.txt
grep -q 'single-float ABI' attributes.txt
grep -Fq 'Tag_RISCV_arch: "rv32i2p1_m2p0_a2p1_f2p2_c2p0_zicsr2p0_zmmul1p0_zca1p0_zcf1p0"' attributes.txt
python3 - <<'PY'
from pathlib import Path
import re
def sites(name):
    d={}
    sec=""
    for line in Path(name).read_text().splitlines():
        m=re.match(r"Relocation section '([^']+)'",line)
        if m:
            sec=m[1]
            continue
        f=line.split()
        if len(f)>=5 and f[2].startswith("R_RISCV_") and f[4] in ("free","radio_osi_free"):
            d.setdefault(f[4],[]).append((sec,f[0],f[2]))
    return d
a,b,c=map(sites,("before-relocs.txt","scoped-relocs.txt","combined-relocs.txt"))
assert a=={"free":[(".rela.text.hal_crypto_set_key_entry","0000017a","R_RISCV_CALL")]}
assert b=={"radio_osi_free":a["free"]}
assert c.get("free")==[(".rela.text.release","00000028","R_RISCV_CALL_PLT")]
assert any(x[0]==".rela.text.hal_crypto_set_key_entry" for x in c["radio_osi_free"])
u=set(Path("scoped-undefined.txt").read_text().splitlines())
v=set(Path("combined-undefined.txt").read_text().splitlines())
assert "radio_osi_free" in u and "free" not in u
assert u-v=={"radio_osi_free"}
assert v-u=={"abort","aligned_alloc","free","pthread_mutex_lock","pthread_mutex_unlock"}
print("SCOPED",b)
print("LINKED",c)
print("UNDEFINED",len(u),"->",len(v))
print("RUNTIME_ADDITIONS",sorted(v-u))
PY
sha256sum *.o *.txt *.c sdkconfig.h > SHA256SUMS
sha256sum -c SHA256SUMS
echo S31_WIFI_SCOPED_FREE_LINK_PASS
