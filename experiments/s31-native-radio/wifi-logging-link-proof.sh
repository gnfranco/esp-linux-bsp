#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Experimental link-only proof; no execution of radio archives.
set -eu
export LC_ALL=C
test "$#" = 3
src=$(realpath "$(dirname "$0")")
closure=$(realpath "$1")
p=$2
case "$p" in */*) p="$(realpath "$(dirname "$p")")/$(basename "$p")";; esac
for t in gcc ld nm readelf; do command -v "${p}$t" >/dev/null; done
printf '%s  %s\n' 4fd15d2d3e0296886c164c613d0d429498927e9fd0c19d7a34881024e2856e09 "$closure" | sha256sum -c -
umask 077
mkdir "$3"
cd "$3"
cc -std=c11 -Wall -Wextra -Werror -Wformat=2 -O2 "$src/wifi-userspace-log.c" "$src/test-wifi-userspace-log.c" -o host-test
./host-test
cc -std=c11 -Wall -Wextra -Werror -Wformat=2 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie "$src/wifi-userspace-log.c" "$src/test-wifi-userspace-log.c" -o host-sanitized
ASAN_OPTIONS=detect_leaks=1 ./host-sanitized
"${p}gcc" -march=rv32imafc -mabi=ilp32f -std=c11 -O2 -Wall -Wextra -Werror -Wformat=2 -static "$src/wifi-userspace-log.c" "$src/test-wifi-userspace-log.c" -o test-rv32
"${p}gcc" -march=rv32imafc -mabi=ilp32f -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -c "$src/wifi-userspace-log.c" -o log.o
libc=$("${p}gcc" -march=rv32imafc -mabi=ilp32f -print-file-name=libc.a)
libgcc=$("${p}gcc" -march=rv32imafc -mabi=ilp32f -print-libgcc-file-name)
printf '%s  %s\n' 3dda872fd193788b21329bee0aeb7a34af52e0e1f6dc5218497cf61313f20f35 "$libc" 3f02b11387714d45babfce91c4f77d16ff0f6fbb082b9b1cd45f9d398c6c6835 "$libgcc" | sha256sum -c -
"${p}ld" -m elf32lriscv -r "$closure" -o control.o
"${p}ld" -m elf32lriscv -r "$closure" log.o -o log-link.o
"${p}ld" -m elf32lriscv -r log-link.o -o runtime-control.o
"${p}ld" -m elf32lriscv -r log-link.o --start-group "$libc" "$libgcc" --end-group -Map=runtime.map -o runtime.o
for f in control log-link runtime-control runtime; do
    "${p}nm" -u "$f.o" | awk '{print $NF}' | sort -u > "$f.txt"
done
"${p}readelf" -rW runtime.o > runtime-relocs.txt
"${p}readelf" -h -A runtime.o > runtime-attributes.txt
grep -Fq 'Tag_RISCV_arch: "rv32i2p1_m2p0_a2p1_f2p2_c2p0_zicsr2p0_zmmul1p0_zca1p0_zcf1p0"' runtime-attributes.txt
grep -q 'single-float ABI' runtime-attributes.txt
python3 - <<'PY'
from pathlib import Path
import re
def names(f):return set(Path(f+".txt").read_text().splitlines())
a,b,c,d=map(names,("control","log-link","runtime-control","runtime"))
assert a-b=={"net80211_printf","phy_printf","pp_printf"}
assert b-a=={"stderr","vfprintf"}
assert b==c
assert c-d==set("abort aligned_alloc free memcmp memcpy memmove memset pthread_mutex_lock pthread_mutex_unlock putchar puts sprintf stderr strcpy strlen strncmp strncpy strnlen vfprintf".split())
assert not d-c
assert len(d)==148
sec=""
sites=[]
for line in Path("runtime-relocs.txt").read_text().splitlines():
    m=re.match(r"Relocation section '([^']+)'",line)
    if m:sec=m[1]
    elif sec==".rela.text.hal_crypto_set_key_entry":
        f=line.split()
        if len(f)>=5 and f[2].startswith("R_RISCV_") and f[4] in ("free","radio_osi_free"):
            sites.append((f[0],f[2],f[4]))
assert sites==[("0000017a","R_RISCV_CALL","radio_osi_free")]
print("MATCHED_DELTA",len(a),len(b),len(c),len(d))
print("SCOPED_FREE_PRESERVED",sites)
PY
sha256sum *.o *.txt *.map host-test host-sanitized test-rv32 > SHA256SUMS
sha256sum -c SHA256SUMS
echo S31_WIFI_LOGGING_RUNTIME_LINK_PASS
