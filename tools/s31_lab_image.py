#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Assemble the S31 lab v1 image from the exact locally validated artifacts.

Does not download firmware, access a board, or flash anything.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

BSP = Path(__file__).resolve().parents[1]
DTS = "esp32s31-function-core-board-1"
INPUTS = {
    "u-boot-spl-dtb.bin": "4c3d7a579270be8e008c9db2ff623f2dce31c085b19b3b66b5bdc7ad89d17e84",
    "u-boot.itb": "89434b08fc88f65a66539bae8e41e7bb884bf77667b0f2fe6b5c1f8009fbe818",
    "rootfs.cramfs": "0e68103aafeefdbdb22202b9a950edc04a6c96734d1e12af4431ebdad2fee086",
    "xipImage": "5fe191d8e189a229f68848791bdb22b851b1b1922ca3f4b0a150030ba0f406ed",
    DTS + ".dtb": "121ff20b8e36135032b7e21ee27673a20e6994490dd9890c2a382923c74a016e",
    DTS + "-test-generic.dtbo": "ec21e1125620be522fc7aed123f0aa325d0a30592472c60b58630561393970f1",
    DTS + "-test-can-loopback.dtbo": "997d82130c54890c4c132d17d4a878156ab8b3c2415baa59f56a845098ad4b57",
}
OUTPUT_HASHES = {
    "spl_app.bin": "a24730be76890fbb373e876a81a4ed00aec2cbd6d650f3704e2e6e5d88fe5053",
    DTS + ".dtb": "090508d75505c389b915dd868b49c180650ec066876a803ae28394bc644eac42",
    "board-can-selftest.dtb": "87c8fe870ac985f707aed9974f649d3486ceae12ae987bc597d84a8ce0126106",
}
SLOTS = [
    (0x2000, "spl_app.bin"),
    (0x100000, "u-boot.itb"),
    (0x300000, DTS + ".dtb"),
    (0x500000, "xipImage"),
    (0xc00000, "rootfs.cramfs"),
]
FLASH_SIZE = 0x1000000


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(path, expected):
    if digest(path) != expected:
        raise ValueError(f"SHA-256 mismatch: {path}")


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def dt_status(fdtget, dtb, node):
    return subprocess.check_output(
        [str(fdtget), str(dtb), node, "status"], text=True
    ).strip()


def assemble(a):
    dts = a.kernel_build / "arch/riscv/boot/dts/espressif"
    sources = {
        name: (a.boot_images if name in
               ("u-boot-spl-dtb.bin", "u-boot.itb", "rootfs.cramfs")
               else a.kernel_build / "arch/riscv/boot" if name == "xipImage"
               else dts) / name
        for name in INPUTS
    }
    for name, path in sources.items():
        check(path, INPUTS[name])

    # Only create a new private output directory, never overwrite a checkpoint.
    out = a.output.resolve()
    out.mkdir(mode=0o700, parents=True, exist_ok=False)
    for name in ("u-boot-spl-dtb.bin", "u-boot.itb", "rootfs.cramfs", "xipImage"):
        shutil.copyfile(sources[name], out / name)
    normal = out / (DTS + ".dtb")
    selftest = out / "board-can-selftest.dtb"
    fdtoverlay = a.host_dir / "bin/fdtoverlay"
    fdtget = a.host_dir / "bin/fdtget"
    run(fdtoverlay, "-i", sources[DTS + ".dtb"], "-o", normal,
        sources[DTS + "-test-generic.dtbo"])
    run(fdtoverlay, "-i", normal, "-o", selftest,
        sources[DTS + "-test-can-loopback.dtbo"])
    for node in ("/soc/can@20394000", "/soc/can@20395000"):
        if dt_status(fdtget, normal, node) != "disabled":
            raise ValueError(f"unexpected enabled CAN node in normal profile: {node}")
        if dt_status(fdtget, selftest, node) != "okay":
            raise ValueError(f"self-test CAN node not active: {node}")

    env = dict(os.environ, HOST_DIR=str(a.host_dir.resolve()),
               ESP_ESPTOOL=str(a.esptool.resolve()))
    run(BSP / "tools/buildroot_post_image.sh", "esp32s31", "nor", out, env=env)
    for name, expected in OUTPUT_HASHES.items():
        check(out / name, expected)

    lines = (out / "flash_args").read_text().splitlines()
    expected_settings = "--flash-mode dio --flash-freq 80m --flash-size 16MB"
    if lines[0].strip() != expected_settings:
        raise ValueError("unexpected flash settings")
    args = [(int(line.split()[0], 0), line.split()[1]) for line in lines[1:]]
    if args != SLOTS:
        raise ValueError("unexpected flash_args slot map")
    image = (out / "s31_full_flash.bin").read_bytes()
    if len(image) > FLASH_SIZE:
        raise ValueError("image exceeds flash")
    slots = []
    for i, (offset, name) in enumerate(SLOTS):
        data = (out / name).read_bytes()
        end = SLOTS[i + 1][0] if i + 1 < len(SLOTS) else FLASH_SIZE
        if offset + len(data) > end:
            raise ValueError(f"slot overflow: {name}")
        if image[offset:offset + len(data)] != data:
            raise ValueError(f"merged image slot mismatch: {name}")
        slots.append({"file": name, "offset": f"0x{offset:06x}",
                      "size": len(data), "capacity": end - offset,
                      "sha256": digest(out / name)})
    manifest = {
        "profile": "s31-lab-v1-peripherals",
        "board": "ESP32-S31 Function-CoreBoard-1 rev 0.0, 16 MiB NOR / PSRAM",
        "linux_commit": "9b41c9c5df88ed883b05662187e431bb3452e491",
        "linux_tree": "f179d36a9a2bf63733fd4d0567d174c82e96c545",
        "uboot_commit": "7c053400864fbe0e8479a5ab6cebfa57441e906c",
        "opensbi_commit": "0940d8a5834d02b93e2be0e60ef6505767862875",
        "buildroot_commit": "7ce1b3ce0234fee7825373784d55b8abe247a550",
        "buildroot_external_commit": "de2f5a653ec184c5454a380872e30fa491f7b933",
        "inputs": INPUTS,
        "slots": slots,
        "image": {"file": "s31_full_flash.bin", "bytes": len(image),
                  "sha256": digest(out / "s31_full_flash.bin")},
        "can_selftest_dtb": {"file": selftest.name, "sha256": digest(selftest),
                            "offset": "0x300000",
                            "requires_unconnected_pads": [35, 36]},
        "limitations": ["No native Wi-Fi or Bluetooth in this image",
                        "CAN transceiver/bus remains unvalidated",
                        "CAN controllers disabled in the default DTB"],
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    outputs = [p for p in out.iterdir() if p.is_file() and p.name != "SHA256SUMS"]
    (out / "SHA256SUMS").write_text("".join(
        f"{digest(p)}  {p.name}\n" for p in sorted(outputs)))
    print(f"LAB_IMAGE_PASS {out / 's31_full_flash.bin'}")
    print(f"SHA256={manifest['image']['sha256']}, bytes={len(image)}")
    for slot in slots:
        print(f"{slot['file']}: {slot['size']}/{slot['capacity']} bytes")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--boot-images", type=Path, required=True)
    p.add_argument("--kernel-build", type=Path, required=True)
    p.add_argument("--host-dir", type=Path, required=True)
    p.add_argument("--esptool", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    assemble(p.parse_args())


if __name__ == "__main__":
    main()
