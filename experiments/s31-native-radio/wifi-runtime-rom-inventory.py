#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inventory surviving ROM references; no mapping, linking or execution."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("maps",type=Path)
p.add_argument("sites",type=Path)
a=p.parse_args()
lock=json.loads((Path(__file__).resolve().parents[2]/"dependencies/s31-native-wifi-first.lock.json").read_text())
raw=a.sites.read_bytes()
if hashlib.sha256(raw).hexdigest()!="967826b964b64873a0f64eaf9328fd6a5ab739aaf0a11ca05f6663340afa3c14":
    raise ValueError("unexpected relocation inventory")
sites=json.loads(raw)
found=collections.defaultdict(list)
for name,sha in sorted(lock["rom_map_git_blob"].items()):
    b=(a.maps/name).read_bytes()
    if hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()!=sha:
        raise ValueError("unexpected map "+name)
    body=re.sub(r"/\*.*?\*/","",b.decode(),flags=re.S)
    for symbol,value in re.findall(r"(?m)^[ \t]*(?:PROVIDE(?:_HIDDEN)?\s*\(\s*)?([A-Za-z_][A-Za-z0-9_.$]*)\s*=\s*(0x[0-9a-fA-F]+)\s*\)?\s*;",body):
        if symbol in sites:found[symbol].append((name,int(value,16)))
def region(x):
    if 0x2f800000<=x<0x2f850000:return "ROM_aperture"
    if 0x2f07afb0<=x<0x2f080000:return "IDF_ROM_reserved_SRAM"
    return "other"
records={};counts=collections.Counter()
for name,refs in sorted(sites.items()):
    addresses=found[name]
    unique={v for _,v in addresses}
    r=region(next(iter(unique))) if len(unique)==1 else "unmapped" if not unique else "ambiguous"
    calls=sum(n for k,n in refs.items() if k.split()[-1] in ("R_RISCV_CALL","R_RISCV_CALL_PLT","R_RISCV_JAL"))
    total=sum(refs.values())
    kind="call_only" if calls==total else "mixed" if calls else "noncall_only"
    counts[r+"/"+kind]+=1
    records[name]={"region":r,"maps":[[k,hex(v)] for k,v in addresses],
                   "relocation_class":kind,"call_relocations":calls,
                   "other_relocations":total-calls,"sites":refs}
if len(records)!=164 or len([v for v in records.values() if v["maps"]])!=157:
    raise ValueError("unexpected remaining symbol count")
print(json.dumps({"counts":dict(counts),"records":records,
    "limitations":"Relocation type and address region only. Noncall includes pointer/data references. No proof of callable ROM, initialized data, privilege, MMIO ownership or runtime ABI."},indent=2,sort_keys=True))
