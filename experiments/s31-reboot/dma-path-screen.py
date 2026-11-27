#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Matched diagnostic snapshots and verified read-only USB workload.

The kernel counters are cumulative since boot, per CPU and DMA device.
Snapshots are not atomic: compare quiescent phases, treating network
background activity independently. No device addresses or payloads are logged.
A fresh boot is required for both profiles.
"""
import argparse
import importlib.util
import json
import pathlib
import re
import subprocess
import time
from collections import Counter

import serial

EVENTS = frozenset(("direct", "force_ok", "force_fail", "addr_ok", "addr_fail",
                    "align_ok", "align_fail", "dwc2_temp_ok", "dwc2_temp_fail"))
BINS = frozenset(("0-16", "17-32", "33-64", "65-96", "97-127", "128+"))
DIRECTIONS = frozenset(("bidi", "to", "from"))


def parse_stats(text):
    counters = {}
    lost = {}
    header = False
    for line in text.splitlines():
        if line == "# cpu device direction bytes event count":
            header = True
        elif re.fullmatch(r"# lost \d+ \d+", line):
            _, _, cpu, count = line.split()
            cpu, count = int(cpu), int(count)
            if cpu in lost or count:
                raise ValueError("duplicate CPU or lost events")
            lost[cpu] = count
        elif re.match(r"^\d+ ", line):
            fields = line.split()
            if len(fields) != 6:
                raise ValueError("invalid row")
            cpu, device, direction, size, event, count = fields
            key = (int(cpu), device, direction, size, event)
            if (direction not in DIRECTIONS or size not in BINS or
                    event not in EVENTS or key in counters or
                    not 0 < int(count) < 2**32-1):
                raise ValueError("invalid or saturated counter")
            counters[key] = int(count)
    if not header or set(lost) != {0, 1} or not counters:
        raise ValueError("missing S31 diagnostic profile")
    if not all(key[0] in lost for key in counters):
        raise ValueError("unexpected CPU in counters")
    return counters


def delta(before, after):
    if any(after.get(key, 0) < val for key, val in before.items()):
        raise ValueError("counter decreased (reset or inconsistent snapshot)")
    return {key: val - before.get(key, 0) for key, val in after.items()
            if val > before.get(key, 0)}


def summarize(counters):
    total = Counter()
    for (_, device, direction, size, event), count in counters.items():
        total[(device, direction, size, event)] += count
    return [dict(zip(("device", "direction", "size", "event", "count"),
                     (*key, value))) for key, value in sorted(total.items())]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--helper", type=pathlib.Path, required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--expect-alignment", choices=("baseline", "retained"),
                    required=True)
    args = ap.parse_args()
    args.out.mkdir(mode=0o700)
    spec = importlib.util.spec_from_file_location("helper", args.helper)
    h = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(h)
    check = subprocess.run(["fuser", args.port], capture_output=True)
    if check.returncode != 1 or check.stdout or check.stderr:
        raise RuntimeError("UART not confirmed free")
    s = serial.Serial(port=None, baudrate=115200, timeout=.15,
                      write_timeout=5, exclusive=True)
    s.port = args.port
    s.dtr = s.rts = False
    mounted = False
    results = {"profile": args.expect_alignment, "usb_sha256": []}
    try:
        s.open()
        h.shell(s)
        time.sleep(3)
        def run(command, seconds=30):
            return h.command(s, command, seconds=seconds)[0]

        info = run("uname -m; cat /sys/devices/system/cpu/online; "
                   "cat /proc/mounts; cat /sys/class/block/sda/device/vendor; "
                   "cat /sys/class/block/sda/device/model; "
                   "dmesg | grep 'retaining 64-byte DMA cache alignment' || true")
        lines = info.splitlines()
        if ("riscv32" not in lines or "0-1" not in lines or
                " / cramfs ro" not in info or
                not any(x.strip() == "Kingston" for x in lines) or
                not any(x.strip() == "DataTraveler 3.0" for x in lines) or
                any(x.startswith("/dev/sda") for x in lines)):
            raise RuntimeError("unexpected S31 profile or USB medium")
        retained = any(re.match(r"^\s*\[\s*\d+\.\d+\]", x) and
                       "retaining 64-byte DMA cache alignment" in x for x in lines)
        if retained != (args.expect_alignment == "retained"):
            raise RuntimeError("unexpected alignment profile")
        _, rc = h.command(s, "grep -q ' /sys/kernel/debug debugfs ' /proc/mounts",
                          check=False)
        if rc:
            run("test -d /sys/kernel/debug && "
                "mount -t debugfs -o ro debugfs /sys/kernel/debug")
            mounted = True

        def snap(name):
            time.sleep(2)
            out = run("cat /sys/kernel/debug/s31_dma_diag/stats")
            stats = parse_stats(out)
            (args.out / (name + ".txt")).write_text(out)
            results[name] = summarize(stats)
            return stats

        boot = snap("boot")
        time.sleep(5)
        idle = snap("idle")
        results["idle_delta"] = summarize(delta(boot, idle))

        for skip in (0, 32, 128, 256, 512, 1024, 2048, 4096):
            out = run(f"dd if=/dev/sda bs=4096 skip={skip} count=32 | sha256sum")
            hits = re.findall(r"(?m)^([0-9a-f]{64})\s+-\s*$", out)
            if (len(hits) != 1 or
                    not re.search(r"(?m)^32\+0 records in\s*$", out) or
                    not re.search(r"(?m)^32\+0 records out\s*$", out)):
                raise RuntimeError("short USB read")
            results["usb_sha256"].append({"skip": skip, "sha256": hits[0]})
        usb = snap("usb")
        results["usb_delta"] = summarize(delta(idle, usb))
        results["bytes_read"] = 1048576

        # Network work is limited to the configured, local bench gateway.
        route = run("ip -4 route show default")
        gateway = re.search(r"(?m)^default via ([0-9.]+) dev eth0\b", route)
        if gateway:
            out = run("ping -I eth0 -c 10 -s 1000 -W 2 " + gateway.group(1), 35)
            results["ethernet"] = ("pass" if re.search(
                r"10 packets transmitted, 10 (?:packets )?received", out) else "loss")
            net = snap("ethernet")
            results["ethernet_delta"] = summarize(delta(usb, net))
        else:
            results["ethernet"] = "no_default_gateway"
        (args.out / "result.json").write_text(json.dumps(results, indent=2) + "\n")
        print("DMA_PATH_SNAPSHOT_PASS", args.expect_alignment, flush=True)
        print("USB_READ_BYTES", results["bytes_read"], flush=True)
        print("ETHERNET", results["ethernet"], flush=True)
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
