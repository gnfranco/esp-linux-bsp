#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Relocatable link audit of the selected Wi-Fi scan roots; never execute."""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import subprocess

def need(ok, message):
    if not ok:
        raise ValueError(message)

def run(*args):
    return subprocess.check_output(args, text=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("deps", type=pathlib.Path)
    parser.add_argument("rom_maps", type=pathlib.Path)
    parser.add_argument("cross_prefix")
    parser.add_argument("out", type=pathlib.Path)
    a = parser.parse_args()
    base = pathlib.Path(__file__).resolve().parents[2]
    lock = json.loads((base/"dependencies/s31-native-wifi-first.lock.json").read_text())
    for name, commit in lock["sources"].items():
        repo = a.deps/name
        need(run("git","-C",str(repo),"rev-parse","HEAD").strip()==commit, "wrong revision: "+name)
        need(not run("git","-C",str(repo),"status","--porcelain"), "dirty dependency: "+name)
    archives = []
    for name, sha in lock["archive_sha256"].items():
        f = (a.deps/name).resolve()
        need(hashlib.sha256(f.read_bytes()).hexdigest()==sha, "archive digest: "+name)
        archives.append(str(f))
    maps = {}
    for name, sha in lock["rom_map_git_blob"].items():
        b = (a.rom_maps/name).read_bytes()
        need(hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()==sha,"ROM blob digest: "+name)
        maps[name] = b.decode()
    a.out.mkdir(mode=0o700)
    obj = a.out/"wifi-full-closure.o"
    cmd = [a.cross_prefix+"ld","-m","elf32lriscv","-r","--gc-sections"]
    for sym in lock["roots"]:
        cmd.extend(["-u",sym])
    cmd.extend(["--start-group",*archives,"--end-group","-Map="+str(a.out/"wifi-full-closure.map"),"-o",str(obj)])
    subprocess.run(cmd,check=True)
    raw = run(a.cross_prefix+"nm","-u",str(obj))
    (a.out/"wifi-full-undefined.txt").write_text(raw)
    symbols = [line.split()[-1] for line in raw.splitlines() if line.strip()]
    need(len(symbols)==len(set(symbols)),"duplicate undefined names")
    matches = collections.defaultdict(list)
    for name, body in sorted(maps.items()):
        text = re.sub(r"/\*.*?\*/","",body,flags=re.S)
        for sym in set(re.findall(r"(?m)^[ \t]*(?:PROVIDE(?:_HIDDEN)?\s*\(\s*)?([A-Za-z_][A-Za-z0-9_.$]*)\s*=",text)):
            if sym in symbols:
                matches[sym].append(name)
    (a.out/"wifi-rom-matches.tsv").write_text("".join(s+"\t"+",".join(matches[s])+"\n" for s in symbols if s in matches))
    (a.out/"wifi-nonrom.txt").write_text("".join(s+"\n" for s in symbols if s not in matches))
    result = {"undefined":len(symbols),"rom_name_matches":len(matches),
              "nonrom_name_matches":len(symbols)-len(matches)}
    print(json.dumps(result,sort_keys=True))
    need(result==lock["reference"],"link closure differs from reference")
    print("S31_WIFI_CLOSURE_PASS")
    # These are names only. ROM access, data layout and ABI are not qualified.
if __name__=="__main__":
    main()
