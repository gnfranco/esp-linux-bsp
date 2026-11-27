#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Reuse the pinned Linux source tree's host libfdt; no kernel build needed.
set -eu
[ "$#" -eq 2 ] || { echo "usage: $0 LINUX_SOURCE NEW_OUTPUT_DIR" >&2; exit 2; }
src=$(realpath "$1")/scripts/dtc/libfdt
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$2"
out=$(realpath "$2")
# libfdt is dual GPL/BSD; tool source remains independent.
${HOST_CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror -I"$src" \
    "$here/memory-audit.c" "$src/fdt.c" "$src/fdt_ro.c" "$src/fdt_strerror.c" \
    -o "$out/memory-audit"
