#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare aggregate SWIOTLB debugfs counters on the S31 lab.

The boot medium is identified before reading; USB contents are never
mounted, written, or collected. Only hashes and aggregate counters
are recorded. This does not attribute bounces to an individual device.
"""
import argparse
import importlib.util
import json
import pathlib
import re
import subprocess
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--helper", type=pathlib.Path, required=True)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--expect-alignment", choices=("baseline", "retained"),
                    required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(mode=0o700)
    spec = importlib.util.spec_from_file_location("helper", args.helper)
    h = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(h)
    r = subprocess.run(["fuser", args.port], capture_output=True)
    if r.returncode != 1 or r.stdout or r.stderr:
        raise RuntimeError("UART not confirmed free")
    s = serial.Serial(port=None, baudrate=115200, timeout=.15,
                      write_timeout=5, exclusive=True)
    s.port = args.port
    s.dtr = False
    s.rts = False
    mounted = False
    result = {"profile": args.expect_alignment, "samples": []}
    try:
        s.open()
        h.shell(s)
        time.sleep(3)
        out, _ = h.command(
            s, "uname -m; cat /sys/devices/system/cpu/online; "
            "cat /proc/mounts; cat /sys/class/block/sda/device/vendor; "
            "cat /sys/class/block/sda/device/model; "
            "dmesg | grep 'retaining 64-byte DMA cache alignment' || true")
        lines = out.splitlines()
        if ("riscv32" not in lines or "0-1" not in lines or
                " / cramfs ro" not in out or
                not any(x.strip() == "Kingston" for x in lines) or
                not any(x.strip() == "DataTraveler 3.0" for x in lines) or
                any(x.startswith("/dev/sda") for x in lines)):
            raise RuntimeError("unexpected S31 profile or test medium")
        retained = any(
            re.match(r"^\s*\[\s*\d+\.\d+\]", x) and
            "retaining 64-byte DMA cache alignment" in x for x in lines)
        if retained != (args.expect_alignment == "retained"):
            raise RuntimeError("unexpected alignment profile")
        _, rc = h.command(s, "grep -q ' /sys/kernel/debug debugfs ' /proc/mounts",
                          check=False)
        if rc:
            h.command(s, "test -d /sys/kernel/debug && "
                      "mount -t debugfs -o ro debugfs /sys/kernel/debug")
            mounted = True

        def counters():
            out, _ = h.command(s, "(cd /sys/kernel/debug/swiotlb && "
                               "for f in io_tlb_nslabs io_tlb_used "
                               "io_tlb_used_hiwater; do "
                               "printf '%s=' \"$f\"; cat \"$f\"; done)")
            values = dict(re.findall(
                r"(?m)^(io_tlb_(?:nslabs|used|used_hiwater))=(\d+)\s*$", out))
            if len(values) != 3:
                raise RuntimeError("SWIOTLB counters not available")
            return {k: int(v) for k, v in values.items()}

        result["before"] = counters()
        for skip in (0, 32, 128, 256, 512, 1024, 2048, 4096):
            out, _ = h.command(
                s, f"dd if=/dev/sda bs=4096 skip={skip} count=32 | sha256sum",
                seconds=30)
            hits = re.findall(r"(?m)^([0-9a-f]{64})\s+-\s*$", out)
            if (len(hits) != 1 or
                    not re.search(r"(?m)^32\+0 records in\s*$", out) or
                    not re.search(r"(?m)^32\+0 records out\s*$", out)):
                raise RuntimeError("short USB read")
            result["samples"].append({"skip_blocks": skip, "sha256": hits[0],
                                      "counters": counters()})
        result["after"] = counters()
        result["bytes_read"] = 8 * 32 * 4096
        (args.out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
    finally:
        if mounted:
            try:
                h.command(s, "umount /sys/kernel/debug")
            finally:
                s.close()
        else:
            s.close()


if __name__ == "__main__":
    main()
