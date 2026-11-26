#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse, base64, gzip, hashlib, importlib.util, pathlib, re, secrets, subprocess, time
import serial

class LoggedSerial(serial.Serial):
    def read(self, size=1):
        data = super().read(size)
        self.log.write(data)
        self.log.flush()
        return data

def require(value, msg):
    if not value:
        raise RuntimeError(msg)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--helper", type=pathlib.Path, required=True)
    ap.add_argument("--gzip", type=pathlib.Path, required=True)
    ap.add_argument("--sha256", required=True)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--log", type=pathlib.Path, required=True)
    a = ap.parse_args()
    payload = a.gzip.read_bytes()
    require(len(payload) <= 65536, "payload too big")
    require(hashlib.sha256(gzip.decompress(payload)).hexdigest() == a.sha256, "local ELF digest")
    spec = importlib.util.spec_from_file_location("uart_helper", a.helper)
    h = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(h)
    require(subprocess.run(["fuser", a.port], capture_output=True).returncode == 1, "UART not free")
    s = LoggedSerial(port=None, baudrate=115200, timeout=.15, write_timeout=5, exclusive=True)
    s.port = a.port
    s.dtr = False
    s.rts = False
    s.log = open(a.log, "xb")
    temp = "/tmp/s31-fpu-" + secrets.token_hex(6)
    made = mounted = False
    try:
        s.open()
        h.shell(s)
        out, _ = h.command(s, "uname -m; uname -r; cat /sys/devices/system/cpu/online; grep ' / ' /proc/mounts; grep '^isa' /proc/cpuinfo; grep '^MemAvailable:' /proc/meminfo")
        print(out.strip(), flush=True)
        require("riscv32" in out.splitlines() and "0-1" in out.splitlines() and " / cramfs ro" in out, "wrong profile")
        isa = [line for line in out.splitlines() if line.startswith("isa")]
        require(len(isa) == 2 and all("rv32imafc" in line for line in isa), "F not advertised by both harts")
        print("F_ONLY_CANDIDATE_BOOT_PASS", flush=True)
        _, rc = h.command(s, "grep -q ' /tmp tmpfs ' /proc/mounts", check=False)
        if rc:
            h.command(s, "! grep -q ' /tmp ' /proc/mounts && mount -t tmpfs -o size=1m tmpfs /tmp")
            mounted = True
        h.command(s, f"umask 077; mkdir {temp}")
        made = True
        encoded = base64.b64encode(payload).decode()
        for i in range(0, len(encoded), 700):
            h.command(s, f"printf '%s' '{encoded[i:i+700]}' >> {temp}/t.b64")
        out, _ = h.command(s, f"base64 -d {temp}/t.b64 | gzip -d > {temp}/test && chmod 700 {temp}/test && sha256sum {temp}/test")
        require(any(line.startswith(a.sha256 + " ") for line in out.splitlines()), "target SHA mismatch")
        print("TARGET_SHA256_PASS", a.sha256, flush=True)
        out, rc = h.command(s, f"{temp}/test", seconds=60, check=False)
        print(out.strip(), flush=True)
        lines = out.splitlines()
        require(rc == 0, f"qualification exit {rc}")
        require(any("F=1 D=0 HWPROBE_FD=0" in line for line in lines), "wrong HWCAP/hwprobe")
        for marker in ("FADD_PASS hart=0", "FADD_PASS hart=1", "SIGNAL_PASS", "SMP_CONTEXT_PASS seed=3 harts=0,1", "SMP_CONTEXT_PASS seed=4 harts=0,1", "PTRACE_PASS", "S31_F_ONLY_PASS"):
            require(marker in lines, "missing " + marker)
        print("F_ONLY_HARDWARE_PASS", flush=True)
        out, _ = h.command(s, "cat /sys/devices/system/cpu/online; test -d /sys/class/net/eth0 && echo ETH_PASS; test -d /sys/class/leds/s31:rgb:status && echo RGB_SYSFS_PASS; test -e /dev/i2c-0 && echo I2C_PASS; test ! -e /sys/class/net/can0 && test ! -e /sys/class/net/can1 && echo CAN_DISABLED_PASS; ls /dev/gpiochip*; printf 'CODEC='; for reg in 0xfd 0xfe 0xff; do i2ctransfer -y 0 w1@0x18 \"$reg\" r1 | tr '\\n' ' '; done; echo; if [ -b /dev/sda ]; then dd if=/dev/sda of=/dev/null bs=4096 count=64 2>/dev/null && echo USB_READ_PASS; else echo USB_NOT_CONNECTED; fi; ping -c 3 -W 1 127.0.0.1 | tail -4", seconds=25)
        print(out.strip(), flush=True)
        for marker in ("0-1", "ETH_PASS", "RGB_SYSFS_PASS", "I2C_PASS", "CAN_DISABLED_PASS"):
            require(marker in out.splitlines(), "regression " + marker)
        require(re.search(r"(?m)^CODEC=0x83 0x11 0x01", out), "codec ID")
        require("/dev/gpiochip" in out and re.search(r"3 packets transmitted, 3 (?:packets )?received", out), "GPIO/loopback")
        require("USB_READ_PASS" in out.splitlines() or "USB_NOT_CONNECTED" in out.splitlines(), "USB check")
        print("F_ONLY_REGRESSION_SMOKE_PASS", flush=True)
    finally:
        try:
            if s.is_open:
                if made:
                    h.command(s, f"rm -f {temp}/t.b64 {temp}/test; rmdir {temp}", seconds=5)
                    print("TARGET_ARTIFACT_REMOVED", flush=True)
                if mounted:
                    h.command(s, "umount /tmp", seconds=5)
        finally:
            s.close()
            s.log.close()

if __name__ == "__main__":
    main()
