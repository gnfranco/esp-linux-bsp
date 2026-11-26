#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 DTC_TOOLS_DIR PINNED_LAB_V1_DTB NEW_OUTPUT_DIR" >&2
    exit 2
fi
tools=$(realpath "$1")
base=$(realpath "$2")
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
echo "090508d75505c389b915dd868b49c180650ec066876a803ae28394bc644eac42  $base" | sha256sum -c -
mkdir "$3"
out=$(realpath "$3")
"$tools/dtc" -@ -I dts -O dtb -o "$out/esp32s31-test-f-only.dtbo" "$here/esp32s31-test-f-only.dtso"
"$tools/fdtoverlay" -i "$base" -o "$out/esp32s31-function-core-board-1-f-only.dtb" "$out/esp32s31-test-f-only.dtbo"
"$tools/dtc" -q -I dtb -O dts -o "$out/base.dts" "$base"
"$tools/dtc" -q -I dtb -O dts -o "$out/candidate.dts" "$out/esp32s31-function-core-board-1-f-only.dtb"
python3 - "$out" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
a = (p / 'base.dts').read_text()
b = (p / 'candidate.dts').read_text()
assert a.count('rv32imac_zicsr_zifencei') == 2
a = a.replace('rv32imac_zicsr_zifencei', 'rv32imafc_zicsr_zifencei')
old = '"i", "m", "a", "c", "zicsr", "zifencei"'
new = '"i", "m", "a", "f", "c", "zicsr", "zifencei"'
assert a.count(old) == 2
assert a.replace(old, new) == b
print('DT_CANDIDATE_ONLY_TWO_HART_ISA_CHANGES_PASS')
PY
(cd "$out" && sha256sum *.dtb *.dtbo > SHA256SUMS)
cat "$out/SHA256SUMS"
