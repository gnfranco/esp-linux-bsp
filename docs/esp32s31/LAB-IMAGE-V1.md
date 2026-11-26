# S31 Lab v1 — engineering image, 2026-11-26

The default development image for the ESP-Linux application labs is assembled
from the Espressif NOR BSP and our pinned Linux, U-Boot and OpenSBI contributions.
See the [manifest](../../dependencies/s31-lab-v1.lock.json) for source revisions,
binary hashes and slot capacities. This is a lab snapshot, not a production
release or upstream endorsement.

## Scope

- Linux 6.18 RV32 XIP, dual core, PSRAM, read-only CramFS, corrected reboot.
- Ethernet, GPIO character devices, RMT RGB LED, I2C character device and
  USB host/mass storage, virtual CAN and CTU CAN FD driver.
- Both physical CAN controller nodes are **disabled** in the default DTB.
  The separately packaged self-test DTB applies the verified GPIO-matrix
  loopback overlay to GPIO35 and GPIO36. Only install it with both pads
  physically unconnected; it is not an external differential CAN bus.
- Native Wi-Fi/Bluetooth and PCM audio experiments use an independent
  community radio profile and **are not included here**. The radio ABI,
  reserved memory, flash layout and licensing gates must be addressed before
  unifying the profiles. Physical I2S audio output is also pending.
- Lab apps are compiled and transferred independently to RAM, as in
  [esp-linux-lab](https://github.com/gnfranco/esp-linux-lab). The rootfs is not
  modified and does not include lab-specific credentials.

## Assemble from verified artifacts

Use the exact validated artifacts from the board baseline and CTU CAN FD
test profile, not moving upstream branch tips. The pinned full kernel
configuration is [here](../../configs/esp32s31-lab-v1.kernel.config).
The source tree is reconstructed by the signed baseline series followed by
the experimental CAN series, or by checking out the exact Linux commit in
the manifest (not both). Preserve the official Buildroot and toolchain pins
from the baseline. The images below are an assembly of validated binaries,
not a fresh reproducibility claim for the entire toolchain.

```sh
python3 tools/s31_lab_image.py \
  --boot-images "$BOOT_IMAGES" \
  --kernel-build "$KERNEL_BUILD" \
  --host-dir "$BUILDROOT_HOST" \
  --esptool "$ESPTOOL" \
  --output "$NEW_OUTPUT_DIR"
cd "$NEW_OUTPUT_DIR"
sha256sum -c SHA256SUMS
```

The assembler validates every input hash before creating the output; both
DTB profiles and the wrapped SPL must match pinned hashes. It invokes the
vendor packager and checks every slot byte-for-byte against the merged
image, refusing overlap or flash-size overflow. It does not flash the board.
The merged image is 16,084,992 bytes (15.34 MiB); rootfs slot capacity is
4,194,304 bytes, with 692,224 bytes free in that slot.

## Flashing

Only flash this image to the specified board and verified 16 MiB NOR layout.
Keep an independent backup and confirm exclusive UART access. The generated
`flash_args` is the source of truth for offsets. Use the approved lab flash
workflow from the local connected station:

```sh
esptool --chip esp32s31 --port "$PORT" write-flash @flash_args
esptool --chip esp32s31 --port "$PORT" verify-flash @flash_args
```

For a board already running the same components, update only the necessary
slot after verifying the other slots. To select the isolated CAN self-test,
replace the DTB slot at 0x300000 with `board-can-selftest.dtb`; restore the
default DTB to return to normal labs. Do not connect any external hardware
to the test GPIOs. The source tree and lock are versioned; no binary,
device flash backup, private logs, or radio payloads are committed.

## Earlier hardware evidence

See [USB/I2C/CAN QA](USB-I2C-CAN-QA-20261126.md) and the
[baseline](BASELINE-20261125.md). The CAN loopback DTB and kernel are the exact
previously tested versions; a differential bus with transceivers remains a
separate test. The CAN SSP review is under
[Espressif Linux RFC #2](https://github.com/espressif/linux/pull/2).

## Integrated board validation, 2026-11-26

The four shared flash slots (SPL, FIT, XIP kernel and rootfs) already matched
the manifest. The previous self-test DTB was independently read back and
verified; only the normal DTB slot was written. All five image slots then
passed esptool verify-flash. The final guarded test booted and software-rebooted
the board, with both CPUs online, CramFS mounted read-only, eth0, gpiochip0,
RGB sysfs and i2c-0 present. Both physical CAN network interfaces remained
absent as intended. IPv4 loopback ping passed 3/3 before and after reboot;
this is not a new external Ethernet traffic test. MemAvailable was
10,348–10,356 KiB during smoke tests.

RGB writes and three separate I2C combined transactions returned codec
identification bytes 83 11 01. A connected USB flash drive enumerated and
262,144 bytes were read without mounting or writing it. Earlier verifier
attempts automatically restored the prior DTB because of serial whitespace,
incorrect multi-byte codec reads and interruption of the U-Boot countdown
by the console poller; the corrected final run passed without intervention.
The normal image was left running on the board. An independent assembly
from the same pinned inputs reproduced the merged image, manifest and
checksums byte for byte. No fresh rebuild of every source component was
performed.
