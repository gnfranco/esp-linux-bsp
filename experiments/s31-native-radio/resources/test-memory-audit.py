#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Positive Lab artifacts and controlled negative DTB fixtures; read-only originals."""
import hashlib
import pathlib
import subprocess
import sys
import tempfile

def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: test-memory-audit.py AUDITOR DTC LAB_IMAGES")
    audit, dtc, images = map(lambda x: pathlib.Path(x).resolve(), sys.argv[1:])
    fit = images / "u-boot.itb"
    expected = {
        "esp32s31-function-core-board-1.dtb": "090508d75505c389b915dd868b49c180650ec066876a803ae28394bc644eac42",
        "board-can-selftest.dtb": "87c8fe870ac985f707aed9974f649d3486ceae12ae987bc597d84a8ce0126106",
        "u-boot.itb": "89434b08fc88f65a66539bae8e41e7bb884bf77667b0f2fe6b5c1f8009fbe818",
    }
    for name, digest in expected.items():
        if hashlib.sha256((images / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"unexpected Lab artifact: {name}")
    expected_lines = {
        "HP_SRAM_WINDOW=[0x2f000000,0x2f080000) bytes=524288",
        "ROM_RESERVED_IDF=[0x2f07afb0,0x2f080000) bytes=20560",
        "OPENSBI_FILE_EXTENT_ONLY=[0x2f000000,0x2f020b2c) bytes=133932",
        "LINUX_DEFAULT_DMA_POOL=[0x2f030000,0x2f070000) bytes=262144",
        "LINUX_PSRAM=[0x50000000,0x51000000) bytes=16777216",
        "RADIO_ALLOCATABLE_BYTES=UNKNOWN",
        "S31_MEMORY_STATIC_AUDIT_PASS",
    }
    def run(dtb, ok, reason=None, boot=fit):
        p = subprocess.run([audit, dtb, boot], capture_output=True, text=True)
        if ok:
            if p.returncode or set(p.stdout.splitlines()) != expected_lines:
                raise RuntimeError(p)
        else:
            if not p.returncode or reason not in p.stderr or "S31_MEMORY_STATIC_AUDIT_PASS" in p.stdout:
                raise RuntimeError(p)
    for name in ("esp32s31-function-core-board-1.dtb", "board-can-selftest.dtb"):
        run(images / name, True)
    def node(base="0x2f030000", size="0x40000", flags="linux,dma-default; no-map;"):
        return f'pool@{base[2:]} {{ compatible = "shared-dma-pool"; reg = <{base} {size}>; {flags} }};'
    cases = [
        ("ok", node(), True, None),
        ("missing", "", False, "missing, duplicate"),
        ("double", node() + node("0x2f010000", "0x1000"), False, "missing, duplicate"),
        ("rom", node("0x2f070000", "0x10000"), False, "missing, duplicate"),
        ("opensbi", node("0x2f010000", "0x10000"), False, "OpenSBI payload overlaps"),
        ("unaligned", node("0x2f030004", "0x1000"), False, "missing, duplicate"),
        ("no-map", node(flags="linux,dma-default;"), False, "unsupported default DMA pool"),
        ("reuse", node(flags="linux,dma-default; no-map; reusable;"), False, "unsupported default DMA pool"),
        ("other", node() + "other@2f040000 { reg = <0x2f040000 0x1000>; };", False, "overlaps another reserved region"),
        ("dynamic", node() + "dynamic { size = <0x1000>; };", False, "unresolved dynamic reservation"),
        ("small-pool", node(size="0x1000"), False, "unexpected Lab DMA pool extent"),
        ("overflow", node("0xfffff000", "0x2000"), False, "overflowing RV32 range"),
    ]
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for name, nodes, ok, reason in cases:
            dts = tmp / f"{name}.dts"
            dtb = tmp / f"{name}.dtb"
            dts.write_text('/dts-v1/; / { compatible = "esp,esp32s31"; '
                '#address-cells = <1>; #size-cells = <1>; '
                'memory@50000000 { device_type = "memory"; reg = <0x50000000 0x1000000>; }; '
                'reserved-memory { #address-cells = <1>; #size-cells = <1>; ranges; '
                + nodes + ' }; };')
            subprocess.run([dtc, "-I", "dts", "-O", "dtb", "-o", dtb, dts],
                           check=True, capture_output=True)
            run(dtb, ok, reason)
            print(f"{name}: PASS")
        bad_mem = tmp / "small-memory.dtb"
        source = (tmp / "ok.dts").read_text().replace("0x1000000", "0x1000")
        (tmp / "small-memory.dts").write_text(source)
        subprocess.run([dtc, "-I", "dts", "-O", "dtb", "-o", bad_mem,
                        tmp / "small-memory.dts"], check=True, capture_output=True)
        run(bad_mem, False, "unexpected Linux memory layout")
        bad_fit = tmp / "wrong-firmware.itb"
        (tmp / "wrong-firmware.dts").write_text('/dts-v1/; / { configurations { default = "conf-1"; conf-1 { firmware = "other"; }; }; };')
        subprocess.run([dtc, "-I", "dts", "-O", "dtb", "-o", bad_fit,
                        tmp / "wrong-firmware.dts"], check=True, capture_output=True)
        run(images / "esp32s31-function-core-board-1.dtb", False, "unexpected FIT selected firmware", bad_fit)
        truncated = tmp / "short.itb"
        truncated.write_bytes(fit.read_bytes()[:4096])
        run(images / "esp32s31-function-core-board-1.dtb", False, "truncated OpenSBI payload", truncated)
    print("S31_MEMORY_AUDIT_TEST_PASS positive=3 negative=14")

if __name__ == "__main__":
    main()
