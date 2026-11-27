#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run a reviewed radio ABI, registration or OSI primitive probe in target RAM."""
import argparse
import base64
import hashlib
import pathlib
import re
import secrets
import subprocess
import time

import serial


def shell(s):
    deadline = time.monotonic() + 65
    data = b""
    sent_login = False
    # Opening a CP210x can reset our board. Never interrupt U-Boot autoboot.
    next_probe = time.monotonic() + 15
    while time.monotonic() < deadline:
        data += s.read(s.in_waiting or 1)
        clean = data.replace(b"\r", b"").replace(b"\0", b"")
        if re.search(rb"(?m)^~ #\s*$", clean):
            return
        if not sent_login and re.search(rb"login:\s*$", clean):
            s.write(b"root\r")
            sent_login = True
            data = b""
        if time.monotonic() >= next_probe:
            s.write(b"\r")
            next_probe = time.monotonic() + 5
        data = data[-32768:]
    raise RuntimeError("target console unavailable")


def command(s, cmd, seconds=20, check=True):
    tag = secrets.token_hex(8)
    s.write((cmd + "; printf '\\n__RADIO%s=%s__\\n' " +
             tag + ' "$?"\r').encode())
    pattern = re.compile(rb"(?m)^__RADIO" + tag.encode() + rb"=(\d+)__$")
    end = time.monotonic() + seconds
    data = b""
    while time.monotonic() < end:
        data += s.read(s.in_waiting or 1)
        clean = data.replace(b"\r", b"").replace(b"\0", b"")
        match = pattern.search(clean)
        if match:
            rc = int(match.group(1))
            out = clean[:match.start()].decode(errors="replace")
            if check and rc:
                raise RuntimeError(f"target rc={rc}: {out[-500:]}")
            return out, rc
    raise RuntimeError("target command timeout")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True)
    ap.add_argument("--artifact", required=True, type=pathlib.Path)
    ap.add_argument("--sha256", required=True)
    ap.add_argument("--probe", choices=("metadata", "coex-register", "osi-sem", "osi-timer"), default="metadata")
    a = ap.parse_args()
    payload = a.artifact.read_bytes()
    if len(payload) > 65536 or hashlib.sha256(payload).hexdigest() != a.sha256:
        raise SystemExit("artifact size/digest mismatch")
    access = subprocess.run(["fuser", a.port], capture_output=True)
    if access.returncode != 1 or access.stderr or access.stdout:
        raise SystemExit("UART not confirmed free")
    s = serial.Serial(port=None, baudrate=115200, timeout=.15,
                      write_timeout=5, exclusive=True)
    s.port = a.port
    s.dtr = False
    s.rts = False
    s.open()
    temp = "/tmp/s31-radio-abi-" + secrets.token_hex(6)
    made = mounted = False
    try:
        shell(s)
        out, _ = command(s, "uname -m; uname -r; cat /sys/devices/system/cpu/online; "
                            "grep ' / ' /proc/mounts; grep '^MemAvailable:' /proc/meminfo")
        # Require the known S31 Lab profile, not the SquashFS community image.
        lines = out.splitlines()
        if not ("riscv32" in lines and any(x.startswith("6.18") for x in lines)
                and "0-1" in lines and any(" / cramfs ro" in x for x in lines)):
            raise RuntimeError("not the expected dual-core CramFS lab profile")
        print(out.strip(), flush=True)
        _, rc = command(s, "grep -q ' /tmp tmpfs ' /proc/mounts", check=False)
        if rc:
            command(s, "! grep -q ' /tmp ' /proc/mounts && "
                       "mount -t tmpfs -o size=1m tmpfs /tmp")
            mounted = True
        command(s, f"umask 077; mkdir {temp}")
        made = True
        encoded = base64.b64encode(payload).decode()
        for i in range(0, len(encoded), 500):
            command(s, f"printf '%s' '{encoded[i:i+500]}' >> {temp}/app.b64")
        out, _ = command(s, f"base64 -d {temp}/app.b64 > {temp}/probe && "
                             f"chmod 700 {temp}/probe && sha256sum {temp}/probe")
        if not any(line.startswith(a.sha256 + " ") for line in out.splitlines()):
            raise RuntimeError("target artifact digest mismatch")
        print("TARGET_SHA256_PASS", a.sha256, flush=True)
        out, _ = command(s, f"{temp}/probe", seconds=40 if a.probe in ("osi-sem", "osi-timer") else 20)
        print(out.strip(), flush=True)
        lines = out.splitlines()
        expected = {
            "metadata": {"S31_RADIO_ABI_PASS", "PHY_CAL_VERSION=100",
                         "COEX=2.0.0", "BTDM=39ed4dc"},
            "coex-register": {"COEX_EMPTY", "COEX_NULL_REJECTED",
                              "COEX_VERSION_REJECTED", "COEX_MAGIC_REJECTED",
                              "COEX_REGISTERED_PRIVATE_POINTER",
                              "COEX_REPLACE_IGNORED",
                              "COEX_NULL_AFTER_REGISTER_IGNORED",
                              "S31_COEX_REGISTER_PASS"},
            "osi-sem": {"SEM_CAPACITY_PASS", "SEM_MONOTONIC_TIMEOUT_PASS",
                        "SEM_WAKE_PASS", "SEM_CLOSE_WAKE_ALL_PASS",
                        "SEM_DEFERRED_CANCEL_PASS", "SEM_MPMC_2048_PASS",
                        "SEM_CLOSE_RACE_16_PASS", "SEM_CANCEL_RACE_16_PASS",
                        "S31_OSI_SEM_PASS"},
            "osi-timer": {"TIMER_ORDER_REARM_STALE_PASS",
                          "TIMER_PERIODIC_FIXED_DELAY_PASS",
                          "TIMER_IN_FLIGHT_REARM_PASS", "TIMER_CLOSE_DRAIN_PASS",
                          "S31_OSI_TIMER_PASS"},
        }[a.probe]
        if not expected.issubset(lines):
            raise RuntimeError("probe markers not confirmed")
        out, _ = command(s, "cat /sys/devices/system/cpu/online; "
                            "test -d /sys/class/net/eth0 && echo ETH0_PRESENT; "
                            "grep '^MemAvailable:' /proc/meminfo")
        print(out.strip(), flush=True)
        if "0-1" not in out.splitlines() or "ETH0_PRESENT" not in out.splitlines():
            raise RuntimeError("post-probe baseline not confirmed")
        print({
            "metadata": "S31_RADIO_ABI_HARDWARE_PASS",
            "coex-register": "S31_COEX_REGISTER_HARDWARE_PASS",
            "osi-sem": "S31_OSI_SEM_HARDWARE_PASS",
            "osi-timer": "S31_OSI_TIMER_HARDWARE_PASS",
        }[a.probe], flush=True)
    finally:
        try:
            if made:
                command(s, f"rm -f {temp}/app.b64 {temp}/probe; rmdir {temp}")
                print("TARGET_ARTIFACT_REMOVED", flush=True)
            if mounted:
                command(s, "umount /tmp")
        finally:
            s.close()


if __name__ == "__main__":
    main()
