#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Classify pinned ROM name matches by address, without resolving or calling them."""
import argparse
import collections
import hashlib
import json
import pathlib
import re

def region(addr):
    if 0x2f800000 <= addr < 0x2f850000:
        return "rom_aperture"
    if 0x2f07afb0 <= addr < 0x2f080000:
        return "idf_rom_reserved_sram"
    if 0x2f000000 <= addr < 0x2f080000:
        return "other_hp_sram"
    return "other"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom_maps", type=pathlib.Path)
    parser.add_argument("matches", type=pathlib.Path)
    a = parser.parse_args()
    base = pathlib.Path(__file__).resolve().parents[2]
    lock = json.loads((base/"dependencies/s31-native-wifi-first.lock.json").read_text())
    symbols = [x.split()[0] for x in a.matches.read_text().splitlines() if x.strip()]
    if len(symbols) != 297 or len(set(symbols)) != 297:
        raise ValueError("unexpected match inventory")
    found = collections.defaultdict(list)
    for name, sha in sorted(lock["rom_map_git_blob"].items()):
        raw = (a.rom_maps/name).read_bytes()
        if hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest() != sha:
            raise ValueError("unexpected map: "+name)
        body = re.sub(r"/\*.*?\*/", "", raw.decode(), flags=re.S)
        for sym, value in re.findall(r"(?m)^[ \t]*(?:PROVIDE(?:_HIDDEN)?\s*\(\s*)?([A-Za-z_][A-Za-z0-9_.$]*)\s*=\s*(0x[0-9a-fA-F]+)\s*\)?\s*;", body):
            if sym in symbols:
                found[sym].append((name, int(value, 16)))
    counts = collections.Counter()
    alternatives = {}
    for sym in sorted(symbols):
        locations = found[sym]
        regions = {region(addr) for _, addr in locations}
        counts[next(iter(regions)) if len(regions)==1 else "unresolved_or_mixed"] += 1
        if len({addr for _, addr in locations}) > 1:
            alternatives[sym] = [(name, hex(addr)) for name, addr in locations]
    if counts != {"rom_aperture":194,"idf_rom_reserved_sram":103} or len(alternatives)!=7:
        raise ValueError("unexpected classification")
    print(json.dumps({"region_counts":counts,"alternative_addresses":alternatives,
                      "limitations":"Address ranges only, not symbol type, ABI, privilege, or runtime availability."},
                     indent=2,sort_keys=True))
    print("S31_WIFI_ROM_ADDRESS_AUDIT_PASS")

if __name__ == "__main__":
    main()
