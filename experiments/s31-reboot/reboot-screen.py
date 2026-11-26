#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only reboot screening, no flash writes. Stop if recovery fails."""
import argparse, importlib.util, pathlib, re, subprocess, time
import serial

LIST_EVENT = re.compile(rb"list_(?:add|del) corruption|list_add double add")

class LoggedSerial(serial.Serial):
    def read(self, size=1):
        data=super().read(size)
        self.log.write(data); self.log.flush()
        return data

def free(port):
    r=subprocess.run(["fuser",port],capture_output=True)
    if r.returncode!=1 or r.stdout or r.stderr:
        raise RuntimeError("UART not free")

def wait_shell(s,seconds=25):
    deadline=time.monotonic()+seconds
    buf=b""; sent=False; saw_oops=False; saw_list=False; probe=time.monotonic()+15
    while time.monotonic()<deadline:
        buf+=s.read(s.in_waiting or 1)
        clean=buf.replace(b"\r",b"").replace(b"\0",b"")
        saw_list |= bool(LIST_EVENT.search(clean))
        if re.search(rb"Oops \[#\d+\]|Unable to handle kernel", clean):
            saw_oops=True
        if b"Kernel panic - not syncing" in clean:
            return "LIST_WARNING_PANIC" if saw_list else "KERNEL_PANIC"
        if re.search(rb"(?m)^~ #\s*$",clean):
            return "LIST_WARNING" if saw_list else ("KERNEL_OOPS" if saw_oops else "ROOT")
        if not sent and re.search(rb"login:\s*$",clean):
            s.write(b"root\r"); sent=True; buf=b""
        if time.monotonic()>probe:
            s.write(b"\r");probe=time.monotonic()+5
        buf=buf[-32768:]
    return "LIST_WARNING" if saw_list else ("KERNEL_OOPS" if saw_oops else "TIMEOUT")

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--helper",type=pathlib.Path,
                   default=pathlib.Path(__file__).resolve().parents[1] / "s31-native-radio/run-uart.py")
    p.add_argument("--esptool",required=True)
    p.add_argument("--port",default="/dev/ttyUSB0")
    p.add_argument("--out",type=pathlib.Path,required=True)
    p.add_argument("--warm",type=int,default=10)
    p.add_argument("--hard",type=int,default=10)
    p.add_argument("--settle",type=float,default=0,help="fixed wait after root profile before each reboot")
    p.add_argument("--expected-cmdline",default=None,help="require exact kernel command line")
    p.add_argument("--recover-initial",action="store_true",help="one read-only recovery if initial boot fails")
    p.add_argument("--stop-on-list-warning",action="store_true")
    a=p.parse_args()
    if not 0<=a.warm<=20 or not 0<=a.hard<=20 or not 0<=a.settle<=30: raise SystemExit("invalid trial count")
    a.out.mkdir(mode=0o700)
    spec=importlib.util.spec_from_file_location("helper",a.helper)
    h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
    free(a.port)
    def connect(name):
        free(a.port)
        s=LoggedSerial(port=None,baudrate=115200,timeout=.15,write_timeout=5,exclusive=True)
        s.port=a.port;s.dtr=False;s.rts=False;s.log=(a.out/(name+".uart")).open("xb")
        s.open();return s
    def close(s):
        s.close();s.log.close()
    def profile(s):
        out,_=h.command(s,"uname -m; cat /sys/devices/system/cpu/online; grep ' / ' /proc/mounts; grep '^isa' /proc/cpuinfo; cat /proc/cmdline; uname -v; cat /proc/sys/kernel/random/boot_id")
        lines=out.splitlines()
        if a.expected_cmdline is not None and a.expected_cmdline not in lines:
            raise RuntimeError("wrong kernel command line")
        if "riscv32" not in lines or "0-1" not in lines or " / cramfs ro" not in out: raise RuntimeError("wrong profile")
        isa=[x for x in lines if x.startswith("isa")]
        if len(isa)!=2 or not all("rv32imac_" in x for x in isa): raise RuntimeError("expected v1 no-F")
        ids=re.findall(r"(?m)^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$",out)
        if len(ids)!=1: raise RuntimeError("boot ID absent")
        return ids[0]
    def hard_reset(name):
        free(a.port)
        with (a.out/(name+".esptool")).open("xb") as f:
            r=subprocess.run([a.esptool,"--chip","esp32s31","--port",a.port,"--baud","1500000","flash-id"],stdout=f,stderr=subprocess.STDOUT,timeout=30)
        if r.returncode: raise RuntimeError("ROM reset failed")
    def try_profile(s,prev):
        result=wait_shell(s)
        if result!="ROOT": return result,None
        try:
            new=profile(s)
            if new==prev: return "BOOT_ID_UNCHANGED",None
            return "PASS",new
        except Exception:
            return "PROFILE_ERROR",None
    def recover(name):
        hard_reset(name)
        s=connect(name)
        try:
            state=wait_shell(s)
            if state!="ROOT": raise RuntimeError("recovery "+state+"; stop suite, no flash writes attempted")
            boot=profile(s)
        except Exception:
            close(s);raise
        print("RECOVERY_PASS",name,flush=True)
        return s,boot
    s=connect("initial")
    counters={}
    try:
        initial=wait_shell(s)
        if initial!="ROOT":
            print("INITIAL_BOOT",initial,flush=True)
            if not a.recover_initial: raise RuntimeError("initial shell not confirmed")
            close(s);s=None
            s,prev=recover("initial-recovery")
        else:
            prev=profile(s)
        for typ,n in (("WARM",a.warm),("ROM_RTS",a.hard)):
            count={}
            for i in range(1,n+1):
                name=f"{typ.lower()}-{i:02d}"
                time.sleep(a.settle)
                t=time.monotonic()
                if typ=="WARM":
                    # Do not reopen UART on warm reboot; opening it can reset the board.
                    s.log.close();s.log=(a.out/(name+".uart")).open("xb")
                    s.reset_input_buffer();s.write(b"sync; reboot\r")
                    time.sleep(2)
                else:
                    close(s);s=None
                    hard_reset(name);s=connect(name)
                result,new=try_profile(s,prev)
                count[result]=count.get(result,0)+1
                print(f"{typ} trial={i} result={result} elapsed_s={time.monotonic()-t:.2f}",flush=True)
                if result=="PASS": prev=new
                else:
                    close(s);s=None
                    s,prev=recover(name+"-recovery")
                    if a.stop_on_list_warning and result.startswith("LIST_WARNING"):
                        counters[typ]=count
                        print("REBOOT_SCREEN_SUMMARY",counters,flush=True)
                        print("STOP_ON_LIST_WARNING",name,flush=True)
                        return
            counters[typ]=count
        print("REBOOT_SCREEN_SUMMARY",counters,flush=True)
        print("FINAL_BASELINE_PASS",flush=True)
    finally:
        if s is not None: close(s)

if __name__=="__main__":main()
