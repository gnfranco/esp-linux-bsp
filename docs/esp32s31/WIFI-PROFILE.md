# S31 native Wi-Fi engineering profile

The validated vendor baseline (`s31-validated-20261125`) remains independent of this opt-in experiment. Do not apply the community SPL patch to the vendor BSP or mix the flash maps. This profile uses the October 24 GrieferPig reference image, replacing only its SPL. It does not port RMT v1.2 into that image. A full source rebuild of the community image has not been reproduced.

## Dependencies and provenance

The [lock manifest](../../dependencies/s31-wifi.lock.json) records the exact release asset, repository revision and gitlinks, patched U-Boot tree, patch digest, compiler identity, esptool version and validated SPL binary digest. The release UART identifies its original SPL as `a6139e51`; the release source tree records `d692a84e` as its U-Boot gitlink. Keep both observations; do not claim bit-for-bit source provenance without a reproducible upstream build. The separately downloaded November `fw-v0.3.1` bundle is a research input only, NOT the radio payload tested in the October image.

Do not track or redistribute radio payloads, full flash images, engineering bundles, or device-specific backups in Git. Download authorized assets from their pinned original locations and store them outside the BSP checkout. The experimental SPL patch retains the U-Boot GPL-2.0-or-later licensing, not the BSP Apache-2.0 license. The community SoftMAC frontend is GPL-2.0-only. The separate payload's own licensing notice marks the build engineering-only and requires permission/compatible grant plus exact corresponding source before public release. A module marked GPL does not establish that the combination is redistributable.

## Verify and assemble locally

Use Python 3.9+ and Git. Set `BSP` to this checkout and `CACHE` to a private external directory containing the original release image, research-only assets and validated SPL binary. The verifier checks all SHA-256 values and lengths; it does not fetch or flash anything.

```sh
python3 "$BSP/tools/s31_wifi_profile.py" verify --inputs "$CACHE" --spl "$CACHE/uboot-compat-build/spl-final.bin"
python3 "$BSP/tools/s31_wifi_profile.py" verify-source --source "$UBOOT"
python3 "$BSP/tools/s31_wifi_profile.py" assemble --inputs "$CACHE" --spl "$CACHE/uboot-compat-build/spl-final.bin" --output "$CACHE/s31-engineering.bin"
```

For source recovery, fetch the pinned U-Boot base from the manifest into a clean dedicated checkout, apply `patches/experimental/s31-wifi/0001-psram-compat.patch` with `git apply`, and commit locally (or check out the pinned review commit). The verifier checks the source tree, not the commit metadata, so adding DCO alone does not invalidate the pin. Rebuild with the pinned `riscv32-esp-elf-` GCC 15.2.0 distribution and esptool 5.3.1:

```sh
make -C "$UBOOT" O="$OUT" ARCH=riscv CROSS_COMPILE="$CROSS" espressif_esp32s31_defconfig
"$UBOOT/scripts/config" --file "$OUT/.config" -e ESP32S31_PSRAM_COMPAT_200MHZ
make -C "$UBOOT" O="$OUT" ARCH=riscv CROSS_COMPILE="$CROSS" olddefconfig
make -C "$UBOOT" O="$OUT" ARCH=riscv CROSS_COMPILE="$CROSS" -j8 spl/u-boot-spl-dtb.bin
"${CROSS}objcopy" -I binary -O elf32-littleriscv -B riscv "$OUT/spl/u-boot-spl-dtb.bin" "$OUT/spl.elf"
"${CROSS}objcopy" --change-section-address .data=0x2F040000 --rename-section .data=.text,alloc,load,readonly,code,contents --set-start 0x2F040000 "$OUT/spl.elf" "$OUT/spl-wrapped.elf"
"$ESPTOOL" --chip esp32s31 elf2image --flash-mode dio --flash-freq 80m --flash-size 16MB --output "$OUT/spl_app.bin" "$OUT/spl-wrapped.elf"
```

Fresh builds may differ due to build metadata. Do not replace a pinned binary hash simply to bypass verification: review the new source/toolchain identities, size, hardware validation and record a new lock revision first. The image assembler only accepts the exact tested SPL and preserves all bytes outside the SPL slot. Its output is a local engineering artifact and has not itself been flashed as a complete image. The board was tested by separately replacing the SPL. Save a full private board backup and verify it before any flash operation.

## Validation and remaining gates

- Board Function-CoreBoard-1 rev 0.0; Linux 6.18.0; two CPUs online, 16 MiB PSRAM, `wlan0` registered.
- Original community SPL stalled; early I2C clock enable with 500 MHz MPLL still timed out. Initial 400 MHz MPLL/200 MHz MSPI compatibility profile booted. The 250 MHz default training path remains selectable and compile-tested only.
- Final style-cleaned candidate: both configurations compile, checkpatch zero errors/warnings, SPL flash verify, boot and reboot pass, four passive scans succeeded (eight BSS entries each). Earlier candidates passed fourteen scans total, including five under hashing load. No claim of connection or IP traffic.
- Pending: authorized WPA2 network association, DHCP/ping/throughput; Ethernet coexistence; RMT integration; regulatory database and country-specific active transmission review; steady-state PSRAM timing; multi-revision and prolonged stress; exact full rebuild and licensing review. Preserve known-good vendor rollback.

## Maintenance policy

Keep dependency updates isolated; never track moving branch tips as release inputs. Update source pins, asset digests, test evidence and license notices together. Upstream PRs stay independent. Prefer upstream fixes over permanent forks. Review the radio ABI and dependency closure before switching release families. A future production profile must not depend on engineering-only artifacts.

Community submission: https://github.com/GrieferPig/u-boot-esp32-s31/pull/1
Vendor submission: https://github.com/espressif/u-boot/pull/1
