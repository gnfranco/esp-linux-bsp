#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded UART/tmpfs control, no radio, SRAM lease or storage writes."""
import argparse
import base64
import hashlib
import importlib.util
import json
import pathlib
import re
import secrets
import subprocess
import time

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def payload():
    # Deterministic non-executable bytes. 80 KiB produces 214 UART chunks.
    return hashlib.shake_256(b"S31 RCU UART control v1").digest(81920)

def profile_check(out, profile):
    lines = out.splitlines()
    require("riscv32" in lines and "0-1" in lines, "wrong CPU profile")
    require("ETH0_PRESENT" in lines and "USB_DISK_PRESENT" in lines,
            "peripheral profile differs")
    mounts = [line.split() for line in lines if len(line.split()) == 6]
    roots = [r for r in mounts if r[1] == "/"]
    require(len(roots) == 1 and roots[0][2] == "cramfs" and
            "ro" in roots[0][3].split(","), "rootfs not read-only")
    tmp = [r for r in mounts if r[1] == "/tmp" or r[1].startswith("/tmp/")]
    require(len(tmp) == 1 and tmp[0][1:3] == ["/tmp", "tmpfs"] and
            "rw" in tmp[0][3].split(","), "tmpfs not exclusive")
    marker = "NO_LEASE_NODE" if profile == "baseline" else "LEASE_NODE"
    other = "LEASE_NODE" if profile == "baseline" else "NO_LEASE_NODE"
    require(lines.count(marker) == 1 and other not in lines and "WRONG_LEASE_NODE" not in lines, "wrong lease profile")

def usb_identity(out, expected_usb):
    # Stable topology and descriptors at sampled times, not proof of
    # uninterrupted attachment. This lab uses the pinned Kingston disk.
    matches = re.findall(r"(?m)^USB_ID ([0-9a-f]{4}) ([0-9a-f]{4}) (\d+) (\S+)$",out)
    require(len(matches)==1,"USB identity missing or ambiguous")
    vid,pid,sectors,path=matches[0]
    require(vid+":"+pid == expected_usb, "unexpected USB VID/PID")
    require(sectors=="121077761" and re.search(r"/usb1/1-1/1-1:1\.0/host\d+/target\d+:\d+:\d+/\d+:\d+:\d+:\d+$",path),
            "unexpected USB disk geometry/topology")
    require(any(x.strip()=="Kingston" for x in out.splitlines()) and
            any(x.strip()=="DataTraveler 3.0" for x in out.splitlines()),
            "unexpected USB disk model")
    return (vid,pid,sectors,path)

def boot_id(out):
    ids = re.findall(r"(?m)^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$", out)
    require(len(ids) == 1, "boot id missing or ambiguous")
    return ids[0]

def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("--helper", type=pathlib.Path, required=True)
    a.add_argument("--out", type=pathlib.Path, required=True)
    a.add_argument("--profile", choices=["baseline","uncorrected","aligned"], required=True)
    a.add_argument("--expected-usb", required=True, help="known VID:PID from baseline read-only inventory")
    a.add_argument("--port", default="/dev/ttyUSB0")
    opt = a.parse_args()
    require(re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{4}",opt.expected_usb),"invalid expected USB identity")
    require(hashlib.sha256(opt.helper.read_bytes()).hexdigest() ==
            "3dcca231d1727022472bd6c87ab2f5d89a18286f895b226ce09ecd5b47d0a7cc",
            "unexpected UART helper")
    q = load("uart_capture", opt.helper)
    screen = load("screen", pathlib.Path(__file__).with_name("screen-kernel-log.py"))
    opt.out.mkdir(mode=0o700)
    result = {"profile":opt.profile,"status":"INCOMPLETE","markers":[],
              "alerts":[],"error":None}
    import serial
    owner = subprocess.run(["fuser",opt.port],capture_output=True)
    require(owner.returncode == 1 and not owner.stdout and not owner.stderr,"UART not free")
    s = serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=5,exclusive=True)
    s.port=opt.port
    s.dtr=s.rts=False
    temp="/tmp/s31-rcu-control-"+secrets.token_hex(6)
    made=mounted=False
    healthy=False
    first=None
    usb=None
    log=opt.out/"commands.log"
    raw=opt.out/"console.raw"
    def command(c, seconds=25):
        with log.open("a") as f:
            f.write("\nCOMMAND "+c+"\n");f.flush()
            try:
                out,rc=q.capture_command(s,c,seconds=seconds,paced=True)
            except q.ConsoleCaptureError as e:
                f.write("PARTIAL\n"+e.partial+"\n");raise
            f.write(out+"\nRC "+str(rc)+"\n")
        require(rc==0,"target command failed")
        require(not screen.screen(out),"kernel alert during command")
        return screen.ANSI.sub("",out)
    def mark(m):
        result["markers"].append(m)
        print(m,flush=True)
    def snapshot(name):
        out=command("dmesg",35)
        (opt.out/(name+".dmesg")).write_text(out)
        return out
    def check_boot():
        return boot_id(command("cat /proc/sys/kernel/random/boot_id"))
    def health():
        profile_check(command("uname -m; cat /sys/devices/system/cpu/online; cat /proc/mounts; test -d /sys/class/net/eth0 && echo ETH0_PRESENT; test -e /sys/class/block/sda && echo USB_DISK_PRESENT; if test -c /dev/s31-wifi-sram-lab && test ! -L /dev/s31-wifi-sram-lab; then echo LEASE_NODE; elif test ! -e /dev/s31-wifi-sram-lab && test ! -L /dev/s31-wifi-sram-lab; then echo NO_LEASE_NODE; else echo WRONG_LEASE_NODE; fi"),opt.profile)
    def identity():
        return usb_identity(command(
            'v=$(cat /sys/bus/usb/devices/1-1/idVendor) && '
            'p=$(cat /sys/bus/usb/devices/1-1/idProduct) && '
            'n=$(cat /sys/class/block/sda/size) && '
            'd=$(readlink -f /sys/class/block/sda/device) && '
            'printf "USB_ID %s %s %s %s\\n" "$v" "$p" "$n" "$d" && '
            'cat /sys/class/block/sda/device/vendor /sys/class/block/sda/device/model'),opt.expected_usb)
    def check_tmp():
        command('test ! -L /tmp && test "$(readlink -f /tmp)" = /tmp')
        # Reject nested/stacked mounts in the trusted, otherwise idle lab.
        health()
    def alerts():
        unique={}
        for p in (raw,log,*opt.out.glob("*.dmesg")):
            if p.exists():
                for kind,line in screen.screen(p.read_text(errors="replace")):
                    unique[(kind,line.strip())]=None
        return [{"kind":k,"line":v} for k,v in unique]
    try:
        s.open()
        deadline=time.monotonic()+65
        nextprobe=time.monotonic()+15
        buf=b""
        with raw.open("wb",buffering=0) as f:
            while time.monotonic()<deadline:
                b=s.read(s.in_waiting or 1)
                if b:
                    f.write(b);buf=(buf+b)[-32768:]
                if re.search(rb"(?m)^(?:~ )?#\s*$",buf.replace(b"\r",b"").replace(b"\0",b"")):
                    break
                if time.monotonic()>=nextprobe:
                    s.write(b"\r");nextprobe=time.monotonic()+5
            else:
                raise RuntimeError("no prompt")
        # Give slow USB enumeration a bounded chance to complete.
        command("i=0; while test ! -e /sys/class/block/sda && test \"$i\" -lt 25; do sleep 1; i=$((i+1)); done; test -e /sys/class/block/sda",35)
        first=check_boot()
        health()
        usb=identity()
        before=snapshot("before")
        has_alignment="ESP32-S31: retaining 64-byte DMA cache alignment" in before
        require(has_alignment==(opt.profile=="aligned"),"wrong DMA alignment profile")
        require(not alerts(),"fault before workload")
        healthy=True
        result["usb_identity"]=list(usb)
        mark("CONTROL_PROFILE_PASS")
        # Same read-only debugfs mount in all profiles; never access lease.
        command("! grep -q \" /sys/kernel/debug \" /proc/mounts")
        mounted=True
        command("mount -t debugfs -o ro debugfs /sys/kernel/debug")
        command("grep -q \" /sys/kernel/debug debugfs ro\" /proc/mounts")
        check_tmp()
        made=True
        command("umask 077; mkdir "+temp)
        data=payload()
        encoded=base64.b64encode(data).decode()
        for i in range(0,len(encoded),512):
            command("printf '%s' '"+encoded[i:i+512]+"' >> "+temp+"/payload.b64")
            if i%16384==0:
                print("UART_BASE64_BYTES",i,flush=True)
                if i:
                    snapshot("checkpoint-"+str(i))
                    require(identity()==usb and check_boot()==first,"USB or boot identity changed")
                    check_tmp()
        out=command("base64 -d "+temp+"/payload.b64 > "+temp+"/payload && sha256sum "+temp+"/payload")
        require(any(x.startswith(hashlib.sha256(data).hexdigest()+" ") for x in out.splitlines()),"payload digest mismatch")
        mark("CONTROL_UART_TMPFS_PASS")
        check_tmp()
        command("rm -f "+temp+"/payload.b64 "+temp+"/payload && rmdir "+temp)
        made=False
        command("umount /sys/kernel/debug")
        mounted=False
        mark("CONTROL_CLEANUP_PASS")
        # Capture asynchronous messages beyond the upload; no input.
        end=time.monotonic()+100
        with raw.open("ab",buffering=0) as f:
            while time.monotonic()<end:
                b=s.read(s.in_waiting or 1)
                if b:f.write(b)
        snapshot("after")
        require(check_boot()==first,"boot id changed")
        require(identity()==usb,"USB identity changed")
        health()
        require(not alerts(),"kernel alerts during observation")
        mark("CONTROL_OBSERVATION_COMPLETE")
        result["status"]="OBSERVED"
    except Exception as e:
        result["error"]=str(e)
        print("CONTROL_INCOMPLETE",str(e),flush=True)
    finally:
        # Never blindly send cleanup after a failed console command.
        # Only the caller's verified reset may clear leftovers.
        result.update({"tmpfs_cleanup_pending":made,"debugfs_cleanup_pending":mounted,
                       "profile_confirmed":healthy})
        s.close()
        result["alerts"]=alerts()
        if result["alerts"]:
            result["status"]="INCOMPLETE"
        (opt.out/"result.json").write_text(json.dumps(result,indent=2)+"\n")
    for x in result["alerts"]:
        print("ALERT",x["kind"],x["line"],flush=True)
    print("CONTROL_RESULT",result["status"],len(result["alerts"]),flush=True)
    return 0 if result["status"]=="OBSERVED" and not result["alerts"] else 1

if __name__=="__main__":
    raise SystemExit(main())
