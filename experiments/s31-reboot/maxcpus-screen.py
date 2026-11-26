#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Screen boot stability with volatile U-Boot maxcpus, never saveenv or flash."""
import argparse
import importlib.util
import pathlib
import re
import subprocess
import time
import serial

BOOTCMD = b"setenv bootargs ${bootargsnor}"
BOOTI = b"; booti 0x40400000 - 0x40200000\r"
ROOT = re.compile(rb"(?m)^~ #\s*$")
UBOOT = re.compile(rb"(?m)^esp32s31>\s*$")
LOGIN = re.compile(rb"(?m)login:\s*$")
OOPS = re.compile(rb"Oops \[#\d+\]|Unable to handle kernel")

class LoggedSerial(serial.Serial):
    def read(self, n=1):
        b = super().read(n)
        self.log.write(b)
        self.log.flush()
        return b

def free(port):
    r = subprocess.run(["fuser", port], capture_output=True)
    if r.returncode != 1 or r.stdout or r.stderr:
        raise RuntimeError("UART not confirmed free")

def connect(port, log):
    free(port)
    s = LoggedSerial(port=None, baudrate=115200, timeout=.1,
                     write_timeout=5, exclusive=True)
    s.port = port
    s.dtr = False
    s.rts = False
    s.log = log.open("xb")
    s.open()
    return s

def close(s):
    s.close()
    s.log.close()

def wait_root(s, seconds=30):
    buf = b""
    sent = saw_oops = False
    probe = time.monotonic() + 14
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        buf += s.read(s.in_waiting or 1)
        clean = buf.replace(b"\r", b"").replace(b"\0", b"")
        saw_oops |= bool(OOPS.search(clean))
        if b"Kernel panic - not syncing" in clean:
            return "KERNEL_PANIC"
        if ROOT.search(clean):
            return "KERNEL_OOPS" if saw_oops else "ROOT"
        if not sent and LOGIN.search(clean):
            s.write(b"root\r")
            sent = True
            buf = b""
        if time.monotonic() > probe:
            s.write(b"\r")
            probe = time.monotonic() + 5
        buf = buf[-32768:]
    return "KERNEL_OOPS" if saw_oops else "TIMEOUT"

def intercept(s, cpus):
    # Never send an instruction unless the autoboot banner AND prompt
    # have both been observed after the reboot command.
    buf = b""
    sent = False
    end = time.monotonic() + 15
    while time.monotonic() < end:
        buf += s.read(s.in_waiting or 1)
        clean = buf.replace(b"\r", b"").replace(b"\0", b"")
        if not sent and b"Hit any key to stop autoboot:" in clean:
            s.write(b" ")
            sent = True
        if sent and UBOOT.search(clean):
            suffix = b" maxcpus=1" if cpus == 1 else b""
            s.write(BOOTCMD + suffix + BOOTI)
            return True
        if b"Starting kernel ..." in clean:
            return False
        buf = buf[-32768:]
    return False

def profile(h, s, cpus):
    out, _ = h.command(s, "uname -m; cat /sys/devices/system/cpu/online; grep ' / ' /proc/mounts; grep '^isa' /proc/cpuinfo; cat /proc/cmdline; cat /proc/sys/kernel/random/boot_id")
    lines = out.splitlines()
    expected = "0" if cpus == 1 else "0-1"
    if "riscv32" not in lines or expected not in lines or " / cramfs ro" not in out:
        raise RuntimeError("wrong target profile")
    isa = [x for x in lines if x.startswith("isa")]
    if len(isa) != cpus or not all("rv32imac_" in x for x in isa):
        raise RuntimeError("expected no-F profile")
    cmdlines = [x for x in lines if x.startswith("earlycon=sbi ")]
    if len(cmdlines) != 1 or ("maxcpus=1" in cmdlines[0]) != (cpus == 1):
        raise RuntimeError("boot parameter not verified")
    ids = re.findall(r"(?m)^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$", out)
    if len(ids) != 1:
        raise RuntimeError("boot ID absent")
    return ids[0]

def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("--helper", required=True, type=pathlib.Path)
    a.add_argument("--esptool", required=True)
    a.add_argument("--port", default="/dev/ttyUSB0")
    a.add_argument("--out", required=True, type=pathlib.Path)
    a.add_argument("--pairs", type=int, default=5)
    a.add_argument("--balanced", action="store_true", help="test source/destination core counts")
    p = a.parse_args()
    if not 1 <= p.pairs <= 10:
        raise SystemExit("pairs must be 1..10")
    p.out.mkdir(mode=0o700)
    spec = importlib.util.spec_from_file_location("uart_helper", p.helper)
    h = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(h)
    def recover(label):
        free(p.port)
        with (p.out / (label + ".esptool")).open("xb") as f:
            subprocess.run([p.esptool, "--chip", "esp32s31", "--port",
                            p.port, "--baud", "1500000", "flash-id"],
                           check=True, stdout=f, stderr=subprocess.STDOUT, timeout=30)
        q = connect(p.port, p.out / (label + ".uart"))
        try:
            result = wait_root(q)
            if result != "ROOT":
                raise RuntimeError("recovery " + result)
            bid = profile(h, q, 2)
        except Exception:
            close(q)
            raise
        print("RECOVERY_PASS", label, flush=True)
        return q, bid
    s = connect(p.port, p.out / "initial.uart")
    counts = {1: {}, 2: {}}
    transitions = {}
    current_cpus = 2
    prev = None
    try:
        if wait_root(s) == "ROOT":
            prev = profile(h, s, 2)
        else:
            close(s)
            s = None
            s, prev = recover("initial-recovery")
        # Alternate destinations; restore normal maxcpus after each pair.
        for i in range(1, p.pairs + 1):
            for j, cpus in enumerate((1, 1, 2, 2) if p.balanced else (1, 2), 1):
                source_cpus = current_cpus
                label = f"block-{i:02d}-trial-{j}-from{source_cpus}-to{cpus}"
                s.log.close()
                s.log = (p.out / (label + ".uart")).open("xb")
                s.reset_input_buffer()
                t = time.monotonic()
                s.write(b"sync; reboot\r")
                if not intercept(s, cpus):
                    result = "INTERCEPT_MISSED"
                else:
                    result = wait_root(s)
                    if result == "ROOT":
                        try:
                            new = profile(h, s, cpus)
                            if new == prev:
                                result = "BOOT_ID_UNCHANGED"
                            else:
                                prev = new
                                current_cpus = cpus
                                result = "PASS"
                        except Exception:
                            result = "PROFILE_ERROR"
                print(f"TRIAL block={i} source_cpus={source_cpus} destination_cpus={cpus} result={result} elapsed_s={time.monotonic()-t:.2f}", flush=True)
                key = f"{source_cpus}->{cpus}"
                tally = transitions.setdefault(key, {})
                tally[result] = tally.get(result, 0) + 1
                counts[cpus][result] = counts[cpus].get(result, 0) + 1
                if result != "PASS":
                    close(s)
                    s = None
                    s, prev = recover(label + "-recovery")
                    current_cpus = 2
                    if result in ("INTERCEPT_MISSED", "PROFILE_ERROR"):
                        raise RuntimeError("cannot attribute trial to requested variant")
        print("MAXCPUS_MATRIX_SUMMARY", counts, flush=True)
        print("MAXCPUS_TRANSITIONS", transitions, flush=True)
        print("FINAL_V1_TWO_HART_PASS", flush=True)
    finally:
        if s is not None:
            close(s)

if __name__ == "__main__":
    main()
