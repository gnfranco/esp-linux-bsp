#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build separately; never flash. Arguments: KERNEL_WORKTREE V1_CONFIG NEW_OUTPUT_DIR
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 KERNEL_WORKTREE V1_CONFIG NEW_OUTPUT_DIR" >&2
    exit 2
fi
src=$(realpath "$1")
base=$(realpath "$2")
host=${S31_HOST_TOOLS:-"$HOME/deps/s31-host-tools/root"}
cc=${S31_KERNEL_CROSS_COMPILE:-"$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-"}
export PATH="$host/usr/bin:$PATH"
export BISON_PKGDATADIR="$host/usr/share/bison"
export LD_LIBRARY_PATH="$host/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
printf '71c1077bb6cd8f07a091db0ffd0e7f5bda36ad954cc5bc74bd29960def4f8b38  %s\n' "$base" | sha256sum -c -
"${cc}gcc" --version | head -1
mkdir "$3"
out=$(realpath "$3")
cp "$base" "$out/.config"
cd "$src"
git rev-parse HEAD > "$out/kernel.commit"
scripts/config --file "$out/.config" --enable RISCV_EXPERIMENTAL_F_ONLY
make O="$out" ARCH=riscv CROSS_COMPILE="$cc" olddefconfig > "$out/config.log" 2>&1
scripts/diffconfig "$base" "$out/.config" | tee "$out/config.diff"
test "$(cat "$out/config.diff")" = '+RISCV_EXPERIMENTAL_F_ONLY y'
make -j"${JOBS:-2}" O="$out" ARCH=riscv CROSS_COMPILE="$cc" xipImage dtbs > "$out/build.log" 2>&1
test "$(stat -c %s "$out/arch/riscv/boot/xipImage")" -le 7340032
(cd "$out" && sha256sum arch/riscv/boot/xipImage .config > SHA256SUMS)
cat "$out/SHA256SUMS"
