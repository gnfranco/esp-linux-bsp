#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the F-only bad-sigreturn probe in RAM on an exclusively owned UART."""
import argparse,base64,gzip,hashlib,importlib.util,pathlib,secrets,subprocess
import serial

class LoggedSerial(serial.Serial):
    def read(self,n=1):
        b=super().read(n)
        self.log.write(b)
        self.log.flush()
        return b

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--helper",type=pathlib.Path,required=True)
    p.add_argument("--payload",type=pathlib.Path,required=True)
    p.add_argument("--sha256",required=True)
    p.add_argument("--log",type=pathlib.Path,required=True)
    p.add_argument("--port",default="/dev/ttyUSB0")
    a=p.parse_args()
    payload=a.payload.read_bytes()
    if len(payload)>65536 or hashlib.sha256(gzip.decompress(payload)).hexdigest()!=a.sha256:
        raise RuntimeError("payload size/digest mismatch")
    spec=importlib.util.spec_from_file_location("helper",a.helper)
    h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
    r=subprocess.run(["fuser",a.port],capture_output=True)
    if r.returncode!=1 or r.stdout or r.stderr:
        raise RuntimeError("UART not confirmed free")
    s=LoggedSerial(port=None,baudrate=115200,timeout=.15,write_timeout=5,exclusive=True)
    s.port=a.port;s.dtr=False;s.rts=False;s.log=a.log.open("xb")
    temp="/tmp/s31-fault-"+secrets.token_hex(6);made=mounted=False
    try:
        s.open();h.shell(s)
        out,_=h.command(s,"uname -m; cat /sys/devices/system/cpu/online; grep ' / ' /proc/mounts; grep '^isa' /proc/cpuinfo")
        if "riscv32" not in out.splitlines() or "0-1" not in out.splitlines() or " / cramfs ro" not in out:
            raise RuntimeError("wrong target profile")
        isa=[x for x in out.splitlines() if x.startswith("isa")]
        if len(isa)!=2 or not all("rv32imafc" in x for x in isa):
            raise RuntimeError("F-only DTB required")
        _,rc=h.command(s,"grep -q ' /tmp tmpfs ' /proc/mounts",check=False)
        if rc:
            h.command(s,"! grep -q ' /tmp ' /proc/mounts && mount -t tmpfs -o size=1m tmpfs /tmp")
            mounted=True
        h.command(s,f"umask 077; mkdir {temp}");made=True
        encoded=base64.b64encode(payload).decode()
        for i in range(0,len(encoded),700):
            h.command(s,f"printf '%s' '{encoded[i:i+700]}' >> {temp}/t.b64")
        out,_=h.command(s,f"base64 -d {temp}/t.b64 | gzip -d > {temp}/test && chmod 700 {temp}/test && sha256sum {temp}/test")
        if not any(x.startswith(a.sha256+" ") for x in out.splitlines()):
            raise RuntimeError("target payload hash")
        print("TARGET_SHA256_PASS",flush=True)
        out,rc=h.command(s,f"{temp}/test",seconds=45,check=False)
        print(out.strip(),flush=True)
        if rc or not all(marker in out.splitlines() for marker in
                         ("SIGRETURN_PARTIAL_WRITE_OBSERVED","SIGRETURN_FAULT_PASS")):
            raise RuntimeError("fault test failed or inconclusive")
    finally:
        try:
            if made:h.command(s,f"rm -f {temp}/t.b64 {temp}/test; rmdir {temp}",seconds=5)
            if mounted:h.command(s,"umount /tmp",seconds=5)
        finally:s.close();s.log.close()

if __name__=="__main__":
    main()
