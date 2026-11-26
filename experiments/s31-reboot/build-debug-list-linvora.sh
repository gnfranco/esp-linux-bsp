#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build a diagnostic baseline kernel. No flash access.
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 CLEAN_BASELINE_KERNEL_WORKTREE V1_CONFIG NEW_OUTPUT_DIR" >&2
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
cd "$src"
test "$(git rev-parse HEAD)" = 9b41c9c5df88ed883b05662187e431bb3452e491
test -z "$(git status --porcelain --untracked-files=no)"
mkdir "$3"
out=$(realpath "$3")
cp "$base" "$out/.config"
git rev-parse HEAD > "$out/kernel.commit"
scripts/config --file "$out/.config" --enable DEBUG_LIST
make O="$out" ARCH=riscv CROSS_COMPILE="$cc" olddefconfig > "$out/config.log" 2>&1
scripts/diffconfig "$base" "$out/.config" | tee "$out/config.diff"
# DEBUG_LIST selects LIST_HARDENED. No other config changes allowed.
printf ' DEBUG_LIST n -> y\n LIST_HARDENED n -> y\n' > "$out/expected.diff"
cmp "$out/expected.diff" "$out/config.diff"
grep -q '^CONFIG_SMP=y$' "$out/.config"
! grep -q '^CONFIG_RISCV_EXPERIMENTAL_F_ONLY=y$' "$out/.config"
make -j"${JOBS:-2}" O="$out" ARCH=riscv CROSS_COMPILE="$cc" xipImage dtbs > "$out/build.log" 2>&1
test "$(stat -c %s "$out/arch/riscv/boot/xipImage")" -le 7340032
(cd "$out" && sha256sum arch/riscv/boot/xipImage .config > SHA256SUMS)
cat "$out/SHA256SUMS"
