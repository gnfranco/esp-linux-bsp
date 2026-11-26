#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Read-only test on an explicitly selected USB mass-storage device.
set -eu
test "$#" -eq 1 || { echo "usage: $0 /dev/sdX" >&2; exit 2; }
dev=$1
case "$dev" in /dev/sd[a-z]) ;; *) echo "Select a whole USB disk, not a partition" >&2; exit 2;; esac
name=${dev##*/}
test -b "$dev"
path=$(readlink -f "/sys/class/block/$name/device")
case "$path" in */usb[0-9]/*) ;; *) echo "Not a USB disk" >&2; exit 2;; esac
test "$(cat "/sys/class/block/$name/size")" -ge 512
grep -q ' /tmp tmpfs ' /proc/mounts || { echo "No tmpfs at /tmp" >&2; exit 2; }
tmp=$(mktemp -d /tmp/s31-usb-read.XXXXXX)
trap 'rm -f "$tmp/a" "$tmp/b"; rmdir "$tmp"' EXIT
dd if="$dev" of="$tmp/a" bs=4096 count=64 2>/dev/null
dd if="$dev" of="$tmp/b" bs=4096 count=64 2>/dev/null
test "$(wc -c < "$tmp/a")" -eq 262144
test "$(wc -c < "$tmp/b")" -eq 262144
cmp -s "$tmp/a" "$tmp/b"
echo "USB_STORAGE_READ_PASS bytes=262144 copies=2"
