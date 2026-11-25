# ESP32-S31 Function-CoreBoard-1 Linux bring-up - 2026-11-24

## Hardware and confirmed result
- ESP32-S31 revision v0.0, NOR flash 16 MiB and Octal PSRAM 16 MiB
- ESP-IDF 6.1 independent PSRAM test: 200 MHz; full boot self-test PASS; 2 MiB additional data pattern PASS
- Linux 6.18.0, OpenSBI, U-Boot 2024.07, two RISC-V CPUs, interactive UART shell
- Linux MemTotal 14408 kB; CramFS read-only rootfs, 4 MiB MTD window

## Upstream inputs
- espressif/u-boot: integration/v2024.07-esp32s31
- espressif/linux: integration/v6.18-esp32s31
- espressif/esp-buildroot-external: buildroot/v2025.02-esp32s31
- espressif/esp-linux-bsp: integration/v1.0-esp32s31
- Buildroot defconfig: espressif_esp32s31_function_core_board_nor_hello_world_defconfig

## Fix and verification
- Correct analog I2C clock gating: bit 0 in MODEM_LPCON, before MPLL calibration
- MPLL at 400 MHz rather than 500 MHz; calibrated with MSPI_CAL_END and CAL_STOP
- MSPI 200 MHz using divisor 2; align dummy cycles and read/write latency with ESP-IDF 6.1
- PSRAM identification returns 128 Mbit (16 MiB), then Linux boots from NOR
- Preserve bounded MPLL calibration and fail-safe stop if PSRAM init fails
- Patch at patches/uboot/0001-esp32s31-psram-mpll-400mhz-mspi-200mhz.patch
- Patch dry-run and application validated against pristine snapshots of the three original U-Boot files

Apply the patch with patch -p1 from the root of a clean U-Boot checkout of the integration branch, then rebuild U-Boot and regenerate the full BSP flash image. Patch remains experimental with diagnostic print statements; not production-ready.

## Evidence
- linux-fullimage-boot.log: boot after fully reflashing the 16 MiB NOR image
- linux-fullimage-shell.log: uname, meminfo, cpuinfo, mtd, mount and uptime executed successfully

## Full image
- Filename: s31_full_flash.bin
- Size: 16084992 bytes
- SHA-256: d66917eeeba2d9e4f45a413aed1d5c4fd55754f8ba38ae91e1bafbf3f1ae0fc5

## Remaining observations
- U-Boot FDT reserved-memory warning for 0x2f030000 is non-fatal in the current boot
- Networking incomplete in hello-world image; Ethernet, USB, CAN FD not yet verified
- Factory 16 MiB flash backup preserved on Orion and not included in Git
