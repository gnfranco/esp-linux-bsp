#!/usr/bin/env python3
import argparse,base64,hashlib,importlib.util,pathlib,secrets,subprocess
import serial

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--helper",type=pathlib.Path,required=True)
    ap.add_argument("--gzip",type=pathlib.Path,required=True)
    ap.add_argument("--sha256",required=True)
    ap.add_argument("--port",default="/dev/ttyUSB0")
    a=ap.parse_args()
    spec=importlib.util.spec_from_file_location("uart_helper",a.helper)
    h=importlib.util.module_from_spec(spec); spec.loader.exec_module(h)
    if subprocess.run(["fuser",a.port],capture_output=True).returncode != 1:
        raise SystemExit("UART is not confirmed free")
    s=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=5,exclusive=True)
    s.port=a.port; s.dtr=False;s.rts=False;s.open()
    temp="/tmp/s31-fpu-"+secrets.token_hex(6)
    made=mounted=False
    try:
        h.shell(s)
        out,_=h.command(s,"uname -m; uname -r; cat /sys/devices/system/cpu/online; grep ' / ' /proc/mounts; grep '^isa' /proc/cpuinfo")
        print(out.strip(),flush=True)
        if not ("riscv32" in out.splitlines() and "0-1" in out.splitlines()
                and " / cramfs ro" in out):
            raise RuntimeError("wrong baseline")
        _,rc=h.command(s,"grep -q ' /tmp tmpfs ' /proc/mounts",check=False)
        if rc:
            h.command(s,"! grep -q ' /tmp ' /proc/mounts && mount -t tmpfs -o size=1m tmpfs /tmp")
            mounted=True
        h.command(s,f"umask 077; mkdir {temp}");made=True
        encoded=base64.b64encode(a.gzip.read_bytes()).decode()
        for i in range(0,len(encoded),700):
            h.command(s,f"printf '%s' '{encoded[i:i+700]}' >> {temp}/t.b64")
        out,_=h.command(s,f"base64 -d {temp}/t.b64 | gzip -d > {temp}/test && chmod 700 {temp}/test && sha256sum {temp}/test")
        if not any(line.startswith(a.sha256+" ") for line in out.splitlines()):
            raise RuntimeError("wrong target SHA")
        print("TARGET_SHA256_PASS",a.sha256,flush=True)
        out,rc=h.command(s,f"{temp}/test",seconds=40,check=False)
        print(out.strip(),flush=True)
        if rc!=77 or "FPU_NOT_ADVERTISED" not in out or "F=0 D=0 HWPROBE_FD=0" not in out:
            raise RuntimeError(f"unexpected baseline status {rc}")
        print("FPU_BASELINE_PASS",flush=True)
    finally:
        try:
            if made:
                h.command(s,f"rm -f {temp}/t.b64 {temp}/test; rmdir {temp}")
                print("TARGET_ARTIFACT_REMOVED",flush=True)
            if mounted:h.command(s,"umount /tmp")
        finally:s.close()
if __name__=="__main__":main()
