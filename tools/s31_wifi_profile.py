#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify pinned local engineering inputs; never flash or distribute them."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
LOCK = json.loads((ROOT / 'dependencies/s31-wifi.lock.json').read_text())


def verified(path, spec):
    data = path.read_bytes()
    if len(data) != spec['size'] or hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise SystemExit(f'Integrity mismatch: {path}')
    print(f'PASS {path.name}: {len(data)} bytes')
    return data


def git(source, *args):
    return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['verify', 'verify-source', 'assemble'])
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--spl', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    spec = LOCK['compat_spl']
    verified(ROOT / spec['patch']['path'], spec['patch'])
    if args.action == 'verify-source':
        if not args.source:
            parser.error('--source required')
        if git(args.source, 'status', '--porcelain') or git(args.source, 'rev-parse', 'HEAD^{tree}') != spec['source_tree']:
            raise SystemExit('Source must be clean and match pinned tree')
        print('PASS patched source tree')
        return
    if not args.inputs or not args.spl:
        parser.error('--inputs and --spl required')
    reference = verified(args.inputs / 's31_full_flash.bin', LOCK['reference']['assets']['s31_full_flash.bin'])
    spl = verified(args.spl, spec['validated_binary'])
    if args.action == 'verify':
        for name, entry in LOCK['firmware_research_only']['assets'].items():
            verified(args.inputs / name, entry)
        return
    if not args.output:
        parser.error('--output required')
    if args.output.resolve().is_relative_to(ROOT):
        raise SystemExit('Keep engineering images outside the public BSP checkout')
    start, end = spec['offset'], spec['slot_end']
    if len(spl) > end - start or reference[end:end+4] != bytes.fromhex('d00dfeed'):
        raise SystemExit('Unexpected flash layout or oversized SPL')
    # Preserve all bytes outside SPL partition, including FIT and radio.
    result = reference[:start] + spl.ljust(end-start, b'\xff') + reference[end:]
    if len(result) != len(reference):
        raise SystemExit('Unexpected image size')
    with args.output.open('xb') as f:
        f.write(result)
    print('Local engineering image:', args.output.name, hashlib.sha256(result).hexdigest())


if __name__ == '__main__':
    main()
