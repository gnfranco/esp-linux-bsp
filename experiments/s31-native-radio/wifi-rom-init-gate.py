#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline gate for one exact S31 link closure and a captured read-only snapshot."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess as sp
import tempfile
import uuid

WORDS = {"g_intr_lock_mux":0x2f07ff24, "g_osi_funcs_p":0x2f07ff44,
         "g_wifi_global_lock":0x2f07ff20, "net80211_funcs":0x2f07ff90,
         "pp_wdev_funcs":0x2f07ff48}
CLOSURE = "1a3a30b511390c1618dbff01cb147ac8e9fc11185230665e7c49240836658fac"
INVENTORY = "08e066570078cf3ea41b542fc833f8499059a9ca65926e606d747de7a57fcaac"
HEADER = "0495a83c5c2a28516e4997a69b9f596de9a86a0c"
OFFSETS = {"_spin_lock_create":32, "_mutex_create":72, "_recursive_mutex_create":76,
           "_mutex_delete":80, "_mutex_lock":84, "_mutex_unlock":88,
           "_wifi_pm_sleep_lock_acquire":200, "_wifi_pm_sleep_lock_release":204,
           "_phy_disable":208, "_phy_enable":212, "_wifi_clock_enable":248,
           "_wifi_clock_disable":252, "_calloc_internal":352, "_wifi_malloc":360,
           "_free":176, "_magic":508, "_version":0}

def require(condition, reason):
    if not condition:
        raise ValueError(reason)

def sha256(p, want):
    require(hashlib.sha256(p.read_bytes()).hexdigest()==want, "unexpected artifact "+str(p))

def blob(p, want):
    b=p.read_bytes()
    require(hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()==want,
            "unexpected Git blob "+str(p))

def check_snapshot(s):
    require(s.get("read_only") is True and s.get("profile")=="Lab v1","snapshot profile")
    require(str(uuid.UUID(s["boot_id"]))==s["boot_id"],"boot identity")
    require(s.get("addresses")=={k:hex(v) for k,v in WORDS.items()},"address set")
    obs=s.get("observations")
    require(isinstance(obs,list) and len(obs)==2,"need two observations")
    zero={k:"0x00000000" for k in WORDS}
    require(all(x==zero for x in obs),"nonzero, missing or unexpected word")
    return "OBSERVED_ZERO_ONLY_NOT_EXECUTION_PERMISSION"

def selftest(s):
    check_snapshot(s)
    variants=[]
    t=copy.deepcopy(s);t["observations"][0]["g_osi_funcs_p"]="0x00000004";variants.append(t)
    t=copy.deepcopy(s);t["observations"][1].pop("pp_wdev_funcs");variants.append(t)
    t=copy.deepcopy(s);t["observations"].pop();variants.append(t)
    t=copy.deepcopy(s);t["addresses"]["g_osi_funcs_p"]="0x2f07ff40";variants.append(t)
    t=copy.deepcopy(s);t["read_only"]=False;variants.append(t)
    t=copy.deepcopy(s);t["boot_id"]="unknown";variants.append(t)
    for t in variants:
        try:check_snapshot(t)
        except (ValueError,KeyError,AttributeError,TypeError):continue
        raise RuntimeError("negative snapshot was accepted")
    return len(variants)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("closure",type=Path);p.add_argument("inventory",type=Path)
    p.add_argument("maps",type=Path);p.add_argument("header",type=Path)
    p.add_argument("cross_prefix");p.add_argument("snapshot",type=Path)
    a=p.parse_args()
    sha256(a.closure,CLOSURE);sha256(a.inventory,INVENTORY);blob(a.header,HEADER)
    lock=json.loads((Path(__file__).resolve().parents[2]/"dependencies/s31-native-wifi-first.lock.json").read_text())
    for name in ("esp32s31.rom.net80211.ld","esp32s31.rom.pp.ld"):
        m=a.maps/name;blob(m,lock["rom_map_git_blob"][name])
    records=json.loads(a.inventory.read_text())["records"]
    for k,v in WORDS.items():
        r=records[k]
        require(r["region"]=="IDF_ROM_reserved_SRAM","word region "+k)
        require({int(x[1],16) for x in r["maps"]}=={v},"word address "+k)
        for name,value in r["maps"]:
            m=a.maps/name;blob(m,lock["rom_map_git_blob"][name])
            require(re.search(r"(?m)^\s*"+re.escape(k)+r"\s*=\s*"+re.escape(value)+r"\s*;",m.read_text()),"map definition "+k)
    def tool(n):
        t=shutil.which(a.cross_prefix+n)
        require(t is not None,"missing tool "+n)
        return str(Path(t).absolute())
    objdump=tool("objdump");gcc=tool("gcc");nm=tool("nm")
    def out(*args):
        return sp.check_output(args,text=True)
    undef={l.split()[-1] for l in out(nm,"-u",str(a.closure)).splitlines() if l.split()}
    require(len(undef)==148 and all(records[x]["maps"] for x in undef),"remaining ROM names")
    sram={x for x in undef if records[x]["region"]=="IDF_ROM_reserved_SRAM"}
    require(len(sram)==100,"remaining SRAM names")
    # Compile against the actual pinned header; never execute the generated object.
    with tempfile.TemporaryDirectory(prefix="s31-osi-offset-") as t:
        t=Path(t)
        (t/"sdkconfig.h").write_text("#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n")
        (t/"offsets.c").write_text('#include "wifi_os_adapter.h"\n'
            '_Static_assert(sizeof(void*)==4,"RV32");\n'
            '_Static_assert(sizeof(wifi_osi_funcs_t)==512,"size");\n'
            '_Static_assert(ESP_WIFI_OS_ADAPTER_VERSION==9,"version");\n'
            '_Static_assert(ESP_WIFI_OS_ADAPTER_MAGIC==0xDEADBEAF,"magic");\n'+
            "".join(f'_Static_assert(offsetof(wifi_osi_funcs_t,{k})=={v},"{k}");\n' for k,v in OFFSETS.items()))
        sp.run([gcc,"-march=rv32imafc","-mabi=ilp32f","-Wall","-Wextra","-Werror",
                "-I"+str(t),"-I"+str(a.header.resolve().parent),"-c",str(t/"offsets.c"),"-o",str(t/"offsets.o")],check=True)
    def dis(n,section=None):
        d=out(objdump,"-dr","--section="+(section or ".text."+n),str(a.closure))
        require(f"00000000 <{n}>:" in d,"missing function "+n)
        return d
    def instruction(d,off,code):
        require(re.search(r"(?m)^\s*"+off+r":\s+"+code+r"\s",d),"instruction mismatch "+off)
    def rel(d,off,kind,name):
        require(re.search(r"(?m)^\s*"+off+r":\s+R_RISCV_"+kind+r"\s+"+re.escape(name)+r"\s*$",d),"relocation "+name)
    d=dis("wifi_osi_funcs_register")
    rel(d,"4","LO12_I","g_osi_funcs_p");rel(d,"60","LO12_S","g_osi_funcs_p")
    for off,code in (("8","e325"),("2a","4110"),("2c","4725"),("46","1fc52603"),
                     ("4a","deadc5b7"),("4e","eaf58593"),("60","00a7a023"),
                     ("68","4501"),("6a","8082")):instruction(d,off,code)
    require(re.search(r"bnez\s+a4,68\s+<\.L358>",d),"early success branch")
    d=dis("esp_wifi_init_internal")
    for off,name in (("32","wifi_osi_funcs_register"),("3e","wifi_api_lock"),
                     ("b2","pm_funcs_init"),("c6","wdev_funcs_init"),
                     ("d2","net80211_funcs_init"),("ee","wifi_init_in_caller_task")):rel(d,off,"CALL",name)
    d=dis("pm_funcs_init")
    instruction(d,"c","1607a783");instruction(d,"10","04400593")
    d=dis("wdev_funcs_init")
    rel(d,"22","CALL","wdev_data_init");instruction(d,"32","61800593")
    instruction(d,"38","1607a783");rel(d,"42","LO12_S","pp_wdev_funcs")
    d=dis("net80211_funcs_init")
    rel(d,"1e","CALL","net80211_data_ptr_init");instruction(d,"2e","14c00593")
    instruction(d,"34","1607a783");rel(d,"3e","LO12_S","net80211_funcs")
    d=dis("wifi_api_lock")
    instruction(d,"2e","4bfc");instruction(d,"3e","47fc")
    d=dis("wifi_init_in_caller_task")
    instruction(d,"26","47fc");rel(d,"2a","LO12_S","g_wifi_global_lock")
    instruction(d,"50","539c");rel(d,"54","LO12_S","g_intr_lock_mux")
    instruction(d,"b8","47bc")
    d=dis("wifi_rf_phy_enable",".wifislprxiram.62")
    for off,code in (("56","0c87a783"),("60","0f87a783"),("7c","0d47a783")):instruction(d,off,code)
    d=dis("wifi_rf_phy_disable",".wifislprxiram.64")
    for off,code in (("64","0d07a783"),("80","0fc7a783"),("8e","0cc7a783")):instruction(d,off,code)
    groups={}
    for n,total,expected in (("net80211_data_ptr_init",12,11),("wdev_data_init",31,31)):
        writes=re.findall(r"R_RISCV_LO12_S\s+(\S+)",dis(n))
        require(len(writes)==len(set(writes))==total,"unexpected direct stores "+n)
        matched=sorted(set(writes)&sram)
        require(len(matched)==expected,"unexpected SRAM stores "+n)
        groups[n]=matched
    require(len(set().union(*map(set,groups.values())))==42,"SRAM store union")
    snap=json.loads(a.snapshot.read_text())
    state=check_snapshot(snap)
    n=selftest(snap)
    print(json.dumps({"static_call_path_verified":True,"remaining_sram_names":100,
        "direct_store_groups":groups,"direct_store_union":42,
        "table_allocations_bytes":{"pm":68,"wdev":1560,"net80211":332},
        "snapshot_gate":state,"negative_snapshot_tests":n,
        "execution_allowed":False,"limitations":"Exact static instructions and captured five-word observation only. No live ownership, MMIO, ROM ABI, callbacks, cache or IRQ qualification."},indent=2,sort_keys=True))
    print("S31_WIFI_ROM_INIT_EVIDENCE_PASS")

if __name__=="__main__":
    main()
