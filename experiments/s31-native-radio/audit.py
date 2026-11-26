#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inventory pinned official archives; no linking, loading or RF access."""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import subprocess


def run(*args):
    return subprocess.run(args, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def verify(root, lock):
    for repo, expected in lock["sources"].items():
        p = root / repo
        if run("git", "-C", str(p), "rev-parse", "HEAD").strip() != expected:
            raise ValueError(f"unexpected revision: {repo}")
        if run("git", "-C", str(p), "status", "--porcelain", "--untracked-files=all"):
            raise ValueError(f"dirty dependency: {repo}")
    for name, expected in lock["archive_sha256"].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"archive digest mismatch: {name}")


def symbols(nm, archive):
    defined, undefined = set(), set()
    for line in run(nm, "-g", "-P", str(archive)).splitlines():
        fields = line.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            continue
        name, typ = fields[:2]
        if typ == "U":
            undefined.add(name)
        elif typ not in ("w", "v", "?"):
            defined.add(name)
    return defined, undefined


def inspect(nm, readelf, objdump, root, lock):
    all_defined, all_undefined = set(), set()
    per_archive = {}
    for name in lock["archive_sha256"]:
        archive = root / name
        d, u = symbols(nm, archive)
        all_defined |= d
        all_undefined |= u
        elf = run(readelf, "-h", str(archive))
        classes = collections.Counter(re.findall(r"^\s*Class:\s*(.+)$", elf, re.M))
        machines = collections.Counter(re.findall(r"^\s*Machine:\s*(.+)$", elf, re.M))
        flags = collections.Counter(re.findall(r"^\s*Flags:\s*(.+)$", elf, re.M))
        if set(classes) != {"ELF32"} or set(machines) != {"RISC-V"}:
            raise ValueError(f"unexpected archive format: {name}")
        ops = collections.Counter()
        for line in run(objdump, "-d", str(archive)).splitlines():
            match = re.match(r"^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([a-z][a-z0-9_.]*)", line)
            if match:
                op = match[1]
                if (op.startswith("f") and not op.startswith("fence")) or op.startswith("c.f"):
                    ops[op] += 1
        per_archive[name] = {"defined": len(d), "undefined": len(u),
                             "elf_flags": dict(flags), "fp_mnemonics": dict(ops)}
    external = sorted(all_undefined - all_defined)
    return {"archives": per_archive, "defined_unique": len(all_defined),
            "undefined_unique": len(all_undefined),
            "external_unique": len(external), "external_symbols": external,
            "limitations": "Whole-archive symbol union, not selected link closure; FP mnemonic counts are static, not execution traces."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("deps", type=pathlib.Path)
    parser.add_argument("gcc", type=pathlib.Path)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--lock", type=pathlib.Path, help="alternate audit-only lock")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    lock_path = args.lock or root / "dependencies/s31-native-radio.lock.json"
    lock = json.loads(lock_path.read_text())
    verify(args.deps.resolve(), lock)
    if args.verify_only:
        print("S31_RADIO_PIN_VERIFY_PASS")
        return
    prefix = str(args.gcc.resolve()).removesuffix("gcc")
    print(json.dumps(inspect(prefix+"nm", prefix+"readelf", prefix+"objdump",
                             args.deps.resolve(), lock), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
