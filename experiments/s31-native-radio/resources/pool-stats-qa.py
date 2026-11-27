#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Lab v1 comparison: bounded local Ethernet and USB read-only workload."""
import argparse
import importlib.util
import ipaddress
import json
import pathlib
import re
import secrets
import subprocess
import time

FIELDS = {"page_bytes", "total_pages", "used_pages", "free_pages",
          "tracked_pages", "peak_pages", "largest_allocatable_pages",
          "allocation_failures"}
STATS = "/sys/kernel/debug/dma_global_pool_stats"
SKIPS = (0, 32, 128, 256, 512, 1024, 2048, 4096)

def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)

def parse_stats(out):
    rows = re.findall(r"(?m)^([a-z_]+) ([0-9]+)\s*$", out)
    v = dict((k, int(n)) for k, n in rows if k in FIELDS)
    require(set(v) == FIELDS and sum(k in FIELDS for k, _ in rows) == len(FIELDS),
            "missing or repeated pool statistics")
    require(v["page_bytes"] == 4096 and v["total_pages"] == 64,
            "unexpected Lab v1 pool size")
    require(v["used_pages"] + v["free_pages"] == v["total_pages"] and
            v["tracked_pages"] == v["used_pages"] and
            v["used_pages"] <= v["peak_pages"] <= v["total_pages"],
            "inconsistent pool accounting")
    largest = v["largest_allocatable_pages"]
    require(largest <= v["free_pages"] and
            ((largest == 0 and v["free_pages"] == 0) or
             (largest > 0 and largest & (largest - 1) == 0)),
            "inconsistent allocatable extent")
    return v

class ConsoleCaptureError(RuntimeError):
    def __init__(self, reason, partial):
        super().__init__(reason)
        self.partial = partial

def capture_command(s, cmd, seconds=30):
    """Use the existing command framing, preserving partial output on timeout."""
    tag = secrets.token_hex(8)
    s.write((cmd + "; printf '\\n__RADIO%s=%s__\\n' " +
             tag + ' "$?"\r').encode())
    pattern = re.compile(rb"(?m)^__RADIO" + tag.encode() + rb"=(\d+)__$")
    end = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < end:
        data.extend(s.read(min(s.in_waiting or 1, 4096)))
        clean = bytes(data).replace(b"\r", b"").replace(b"\0", b"")
        match = pattern.search(clean)
        if match:
            return clean[:match.start()].decode(errors="replace"), int(match.group(1))
        if len(data) > 256 * 1024:
            raise ConsoleCaptureError("target output limit exceeded", clean.decode(errors="replace"))
    raise ConsoleCaptureError("target command timeout",
                              bytes(data).replace(b"\r", b"").replace(b"\0", b"").decode(errors="replace"))

def parse_counters(out):
    rows = re.findall(r"(?m)^POOL_IO ([0-9]+) ([0-9]+) ([0-9]+)$", out)
    require(len(rows) == 1, "I/O counters missing or ambiguous")
    return dict(zip(("usb_sectors_read", "eth_rx", "eth_tx"), map(int, rows[0])))

def parse_ping(out):
    rows = re.findall(r"(?m)^[ \t]*(\d+) packets transmitted, (\d+) (?:packets )?received", out)
    require(len(rows) == 1, "Ethernet ping summary missing or ambiguous")
    return tuple(map(int, rows[0]))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--helper", type=pathlib.Path, required=True)
    ap.add_argument("--reference", type=pathlib.Path, required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--profile", choices=("baseline", "instrumented"), required=True)
    ap.add_argument("--usb-only", action="store_true", help="explicit partial test without Ethernet workloads")
    ap.add_argument("--stats-unread", action="store_true", help="instrumented kernel control: do not mount or read debugfs statistics")
    ap.add_argument("--configure-ethernet", action="store_true", help="dedicated bench only: enable eth0 and DHCP if no eth0 gateway; runtime settings persist until reboot")
    a = ap.parse_args()
    if a.stats_unread and (a.profile != "instrumented" or a.usb_only):
        ap.error("--stats-unread requires the instrumented full-scope profile")
    sampled = a.profile == "instrumented" and not a.stats_unread
    ref = json.loads(a.reference.read_text())
    hashes = {x["skip_blocks"]: x["sha256"] for x in ref["samples"]}
    require(set(hashes) == set(SKIPS) and all(re.fullmatch(r"[a-f0-9]{64}", x)
            for x in hashes.values()), "invalid USB reference")
    a.out.mkdir(mode=0o700)
    spec = importlib.util.spec_from_file_location("uart_helper", a.helper)
    h = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(h)
    import serial
    access = subprocess.run(["fuser", "/dev/ttyUSB0"], capture_output=True)
    require(access.returncode == 1 and not access.stdout and not access.stderr,
            "UART not confirmed free")
    s = serial.Serial(port=None, baudrate=115200, timeout=.15,
                      write_timeout=5, exclusive=True)
    s.port = "/dev/ttyUSB0"
    s.dtr = s.rts = False
    result = {"profile": a.profile, "scope": "usb-only" if a.usb_only else "full", "status": "INCOMPLETE", "stats_mode": "sampled" if sampled else ("unread" if a.stats_unread else "baseline"), "samples": [], "phases": {}}
    body_ok = False
    cleanup_errors = []
    mounted = made = False
    tmp = "/tmp/pool-qa-" + secrets.token_hex(6)
    last = None
    def command(cmd, seconds=30, check=True):
        # Keep every response, even if a parser or exit-status check fails.
        with (a.out / "uart-commands.log").open("a") as log:
            log.write(f"\nCOMMAND {cmd}\nHOST_MONOTONIC {time.monotonic():.6f}\n")
            log.flush()
            try:
                out, rc = capture_command(s, cmd, seconds=seconds)
            except ConsoleCaptureError as exc:
                log.write("PARTIAL_OUTPUT_BEGIN\n" + exc.partial + "\nPARTIAL_OUTPUT_END\n")
                log.write(f"TRANSPORT_ERROR {exc!r}\n")
                raise
            except Exception as exc:
                log.write(f"TRANSPORT_ERROR {exc!r}\n")
                raise
            log.write(out + f"\nRC {rc}\nHOST_MONOTONIC_END {time.monotonic():.6f}\n")
        require(not check or rc == 0, f"target command failed: rc={rc}")
        return out, rc
    def run(cmd, seconds=30):
        return command(cmd, seconds)[0]
    def boot():
        out = run("cat /proc/sys/kernel/random/boot_id")
        hits = re.findall(r"(?m)^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$", out)
        require(len(hits) == 1, "boot id missing")
        return hits[0]
    def snap(label, check_boot=True):
        nonlocal last
        if check_boot:
            require(boot() == initial_boot, "unexpected reboot")
        if not sampled:
            return None
        v = parse_stats(run("cat " + STATS))
        if last:
            require(v["peak_pages"] >= last["peak_pages"] and
                    v["allocation_failures"] >= last["allocation_failures"],
                    "nonmonotonic cumulative counters")
        last = v
        result["samples"].append({"phase": label, **v})
        return v
    def counters():
        # Emit a labelled, short line rather than interpreting three
        # unlabelled lines in an asynchronous console transcript.
        out = run('printf "\\nPOOL_IO_DISK_BEGIN\\n"; '
                  'read a b sectors rest < /sys/class/block/sda/stat && '
                  'printf "POOL_IO_RX_BEGIN\\n" && '
                  'read rx < /sys/class/net/eth0/statistics/rx_packets && '
                  'printf "POOL_IO_TX_BEGIN\\n" && '
                  'read tx < /sys/class/net/eth0/statistics/tx_packets && '
                  'printf "POOL_IO %s %s %s\\n" "$sectors" "$rx" "$tx"')
        return parse_counters(out)
    def traffic(phase, usb, net):
        before = counters()
        started = time.monotonic()
        # Launch bounded jobs in the current shell; sample before waiting.
        # Exit statuses are checked by the shell, not inferred from output.
        cmds = []
        if net:
            cmds.append(f"ping -I eth0 -c 10 -s 1000 -W 2 {gateway} >{tmp}/net 2>&1 & n=$!")
        if usb:
            skip = 512 if net else 0
            cmds.append(f"dd if=/dev/sda of=/dev/null bs=65536 skip={skip} count=512 >{tmp}/usb 2>&1 & u=$!")
        run("; ".join(cmds))
        overlap = False
        joint_progress = False
        previous = before
        for i in range(6):
            if usb and net:
                out = run('if kill -0 "$u" 2>/dev/null && kill -0 "$n" 2>/dev/null; then echo BOTH_ACTIVE; fi')
                overlap |= "BOTH_ACTIVE" in out.splitlines()
            snap(f"{phase}-{i}", check_boot=False)
            current = counters()
            joint_progress |= (current["usb_sectors_read"] > previous["usb_sectors_read"] and
                               current["eth_rx"] > previous["eth_rx"] and
                               current["eth_tx"] > previous["eth_tx"])
            previous = current
            time.sleep(.2)
        row = {}
        if usb:
            out = run(f'wait "$u"; r=$?; cat {tmp}/usb; test "$r" = 0', 90)
            require(re.search(r"(?m)^512\+0 records in\s*$", out) and
                    re.search(r"(?m)^512\+0 records out\s*$", out), "short bulk USB read")
            row["usb_bytes"] = 33554432
        if net:
            out, rc = command(f'wait "$n"; r=$?; cat {tmp}/net; test "$r" = 0',
                                seconds=35, check=False)
            result.setdefault("ping_diagnostics", {})[phase] = {"output": out, "rc": rc}
            sent, received = parse_ping(out)
            result["ping_diagnostics"][phase].update(sent=sent, received=received)
            require(rc == 0 and sent == 10 and received == 10,
                    "Ethernet ping criterion failed")
            row["ping_received"] = received
        if usb and net:
            require(overlap and joint_progress, "joint device progress and job overlap not observed")
            row["overlap_observed"] = True
            row["joint_counter_interval_observed"] = True
        row["elapsed_host_s"] = round(time.monotonic() - started, 3)
        after = counters()
        delta = {k: after[k] - before[k] for k in before}
        require(all(v >= 0 for v in delta.values()), "I/O counters decreased")
        if usb:
            require(delta["usb_sectors_read"] > 0, "no block read activity")
        if net:
            require(delta["eth_rx"] >= 10 and delta["eth_tx"] >= 10, "no Ethernet packet activity")
        row["counter_delta"] = delta
        result["phases"][phase] = row
        print("PHASE_PASS", phase, json.dumps(row, sort_keys=True), flush=True)
        snap(phase + "-end")
    try:
        s.open()
        h.shell(s)
        info = run("uname -m; uname -r; cat /sys/devices/system/cpu/online; cat /proc/mounts")
        lines = info.splitlines()
        require("riscv32" in lines and "0-1" in lines and " / cramfs ro" in info and
                any(x.startswith("6.18") for x in lines), "unexpected Lab profile")
        require(not any(re.match(r"^/dev/sd[a-z]", x) for x in lines), "USB is mounted")
        require(any(" /tmp tmpfs " in x for x in lines), "RAM workspace unavailable")
        initial_boot = boot()
        if sampled:
            _, rc = command("grep -q ' /sys/kernel/debug debugfs ' /proc/mounts", check=False)
            if rc:
                run("mount -t debugfs -o ro debugfs /sys/kernel/debug")
                mounted = True
            run("test -r " + STATS)
        snap("boot-console")
        if a.configure_ethernet and not a.usb_only:
            route = run("ip -4 route show default")
            if not re.search(r"(?m)^default via ([0-9.]+) dev eth0\b", route):
                run("ip link set dev eth0 up")
                for attempt in range(10):
                    out = run("printf '\\n'; cat /sys/class/net/eth0/carrier")
                    if "1" in out.splitlines():
                        break
                    time.sleep(2)
                else:
                    raise RuntimeError("no Ethernet carrier after activation")
                snap("ethernet-link-up")
                run("udhcpc -i eth0 -n -q -t 5 -T 3", seconds=30)
                result["dhcp_configured_by_test"] = True
                snap("ethernet-dhcp")
            else:
                result["dhcp_configured_by_test"] = False
        # UART open may reset the board; shell availability precedes
        # asynchronous SCSI enumeration and local network configuration.
        deadline = time.monotonic() + 60
        match = None
        usb_ready = False
        while time.monotonic() < deadline:
            out = run("printf '\\n'; if test -b /dev/sda; then cat /sys/class/block/sda/device/vendor; cat /sys/class/block/sda/device/model; cat /sys/bus/usb/devices/1-1/speed; else echo SDA_ABSENT; fi")
            usb_ready = {"Kingston", "DataTraveler 3.0", "480"}.issubset({x.strip() for x in out.splitlines()})
            route = run("ip -4 route show default")
            match = re.search(r"(?m)^default via ([0-9.]+) dev eth0\b", route)
            if usb_ready and (match or a.usb_only):
                break
            time.sleep(2)
        result["usb_ready"] = usb_ready
        result["local_gateway_ready"] = bool(match)
        require(usb_ready, "known USB medium or high-speed link missing after readiness wait")
        require(match or a.usb_only, "no local Ethernet gateway after readiness wait")
        out = run("cat /sys/class/block/sda/size")
        sectors = [int(x) for x in out.splitlines() if x.isdecimal()]
        require(len(sectors) == 1 and sectors[0] >= 131072, "USB too small for bounded read")
        gateway = str(ipaddress.IPv4Address(match.group(1))) if match else None
        mounts = run("cat /proc/mounts")
        require(not any(re.match(r"^/dev/sd[a-z]", x) for x in mounts.splitlines()),
                "USB is mounted after readiness wait")
        require(boot() == initial_boot, "unexpected reboot during readiness wait")
        run("umask 077; mkdir " + tmp)
        made = True
        snap("idle")
        for skip in SKIPS:
            out = run(f"dd if=/dev/sda bs=4096 skip={skip} count=32 | sha256sum")
            require(re.search(r"(?m)^" + hashes[skip] + r"\s+-\s*$", out) and
                    re.search(r"(?m)^32\+0 records in\s*$", out) and
                    re.search(r"(?m)^32\+0 records out\s*$", out), "USB reference mismatch")
        result["usb_reference_bytes"] = 1048576
        if not a.usb_only:
            traffic("ethernet", False, True)
        traffic("usb", True, False)
        if not a.usb_only:
            traffic("concurrent", True, True)
        snap("final")
        errors = run("dmesg", seconds=45)
        require(not re.search(r"Oops \[|Kernel panic|list_(add|del) corruption|Unable to handle kernel|DMA-API:|swiotlb buffer is full|I/O error, dev sd", errors, re.I),
                "kernel error observed")
        if sampled:
            delta = result["samples"][-1]["allocation_failures"] - result["samples"][0]["allocation_failures"]
            result["allocation_failures_delta"] = delta
            require(delta == 0, "new global pool allocation failures")
        require("0-1" in run("cat /sys/devices/system/cpu/online").splitlines(), "SMP changed")
        body_ok = True
    finally:
        try:
            if made:
                try:
                    command('wait; rm -f ' + tmp + '/usb ' + tmp + '/net; rmdir ' + tmp,
                              seconds=120)
                except KeyboardInterrupt:
                    cleanup_errors.append("RAM workspace: interrupted")
                    raise
                except Exception as exc:
                    cleanup_errors.append("RAM workspace: " + type(exc).__name__)
            if mounted:
                try:
                    run("umount /sys/kernel/debug")
                except KeyboardInterrupt:
                    cleanup_errors.append("debugfs: interrupted")
                    raise
                except Exception as exc:
                    cleanup_errors.append("debugfs: " + type(exc).__name__)
        finally:
            s.close()
            result["cleanup_errors"] = cleanup_errors
            if body_ok and not cleanup_errors:
                result["status"] = ("USB_ONLY_PASS" if a.usb_only else
                                    "UNREAD_CONTROL_PASS" if a.stats_unread else "PASS")
            (a.out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    require(not cleanup_errors, "cleanup failed")
    print("S31_GLOBAL_POOL_USB_ONLY_PASS" if a.usb_only else
          "S31_GLOBAL_POOL_UNREAD_CONTROL_PASS" if a.stats_unread else
          "S31_GLOBAL_POOL_QA_PASS", a.profile, flush=True)

if __name__ == "__main__":
    main()
