#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse,base64,gzip,hashlib,importlib.util,pathlib,re,secrets,subprocess,time
import serial

class LoggedSerial(serial.Serial):
    def read(self,size=1):
        data=super().read(size)
        self.log.write(data); self.log.flush()
        return data

def require(value,msg):
    if not value: raise RuntimeError(msg)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--helper",type=pathlib.Path,required=True)
    ap.add_argument("--extended",type=pathlib.Path,required=True)
    ap.add_argument("--extended-sha",required=True)
    ap.add_argument("--hardfloat",type=pathlib.Path,required=True)
    ap.add_argument("--hardfloat-sha",required=True)
    ap.add_argument("--log",type=pathlib.Path,required=True)
    ap.add_argument("--port",default="/dev/ttyUSB0")
    ap.add_argument("--skip-reboot",action="store_true",
                    help="report functional tests separately from reboot qualification")
    a=ap.parse_args()
    spec=importlib.util.spec_from_file_location("uart_helper",a.helper)
    h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
    require(subprocess.run(["fuser",a.port],capture_output=True).returncode==1,"UART busy")
    s=LoggedSerial(port=None,baudrate=115200,timeout=.15,write_timeout=5,exclusive=True)
    s.port=a.port;s.dtr=False;s.rts=False;s.log=open(a.log,"xb")
    temp="/tmp/s31-fpu-ext-"+secrets.token_hex(6)
    made=mounted=False
    def profile():
        out,_=h.command(s,"uname -m; uname -r; cat /sys/devices/system/cpu/online; grep ' / ' /proc/mounts; grep '^isa' /proc/cpuinfo; cat /proc/sys/kernel/random/boot_id; grep '^MemAvailable:' /proc/meminfo")
        print(out.strip(),flush=True)
        require("riscv32" in out.splitlines() and "0-1" in out.splitlines() and " / cramfs ro" in out,"wrong board profile")
        isa=[x for x in out.splitlines() if x.startswith("isa")]
        require(len(isa)==2 and all("rv32imafc" in x for x in isa),"F not exposed")
        ids=re.findall(r"(?m)^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$",out)
        require(len(ids)==1,"boot ID absent")
        return ids[0]
    def upload(name,path,digest):
        payload=path.read_bytes()
        require(len(payload)<65536 and hashlib.sha256(gzip.decompress(payload)).hexdigest()==digest,"local payload")
        encoded=base64.b64encode(payload).decode()
        for i in range(0,len(encoded),700):
            h.command(s,f"printf '%s' '{encoded[i:i+700]}' >> {temp}/{name}.b64")
        out,_=h.command(s,f"base64 -d {temp}/{name}.b64 | gzip -d > {temp}/{name} && chmod 700 {temp}/{name} && sha256sum {temp}/{name}")
        require(any(x.startswith(digest+" ") for x in out.splitlines()),"target digest")
        print("TARGET_SHA256_PASS",name,digest,flush=True)
    try:
        s.open();h.shell(s)
        before=profile()
        _,rc=h.command(s,"grep -q ' /tmp tmpfs ' /proc/mounts",check=False)
        if rc:
            h.command(s,"! grep -q ' /tmp ' /proc/mounts && mount -t tmpfs -o size=1m tmpfs /tmp")
            mounted=True
        h.command(s,f"umask 077; mkdir {temp}");made=True
        upload("hardfloat",a.hardfloat,a.hardfloat_sha)
        out,rc=h.command(s,f"{temp}/hardfloat",seconds=15,check=False)
        print(out.strip(),flush=True)
        require(rc==0 and "ILP32F_CALL_ABI_PASS" in out.splitlines(),"hardfloat execution")
        upload("extended",a.extended,a.extended_sha)
        out,rc=h.command(s,f"{temp}/extended",seconds=180,check=False)
        print(out.strip(),flush=True)
        require(rc==0 and "S31_F_EXTENDED_PASS" in out.splitlines(),"extended execution")
        for mark in ("SIGNAL_FRAME_MUTATION_PASS","FORK_ISOLATION_PASS","EXEC_RESET_PASS","PTRACE_IMMEDIATE_PASS"):
            require(mark in out.splitlines(),"missing "+mark)
        require("SMP_STRESS_PASS checks=8192" in out,"stress count")
        require(sum(x.startswith("D_REJECT_PASS case=") for x in out.splitlines())==5,"D reject count")
        require(all(any(x.startswith("MISALIGNED_"+kind+"_") for x in out.splitlines()) for kind in ("F","ZCF")),"misaligned results")
        require("PTRACE_PARTIAL_WRITE_OBSERVED" in out.splitlines(),"non-vacuous partial write evidence")
        require("PTRACE_FAULT_PASS" in out.splitlines() and
                "PTRACE_FAULT_CANONICAL=1" in out.splitlines(),"ptrace fault invariant")
        h.command(s,f"rm -f {temp}/hardfloat.b64 {temp}/hardfloat {temp}/extended.b64 {temp}/extended; rmdir {temp}");made=False
        if mounted: h.command(s,"umount /tmp");mounted=False
        print("TARGET_ARTIFACTS_REMOVED",flush=True)
        print("CANDIDATE_FUNCTIONAL_PASS",flush=True)
        if not a.skip_reboot:
            s.reset_input_buffer()
            s.write(b"sync; reboot\r")
            time.sleep(3);h.shell(s)
            after=profile()
            require(after!=before,"boot ID unchanged")
            print("CANDIDATE_SOFTWARE_REBOOT_PASS",flush=True)
        require("PTRACE_FAULT_PASS" in out.splitlines(),"partial-copy canonicalization gap")
        print("EXTENDED_QUALIFICATION_PASS",flush=True)
    finally:
        try:
            if s.is_open:
                if made:h.command(s,f"rm -f {temp}/hardfloat.b64 {temp}/hardfloat {temp}/extended.b64 {temp}/extended; rmdir {temp}",seconds=5)
                if mounted:h.command(s,"umount /tmp",seconds=5)
        finally:
            s.close();s.log.close()
if __name__=="__main__":main()
