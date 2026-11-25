# ESP32-S31 peripheral bring-up (experimental)

> Historical record. For the current patch series use [BASELINE-20261125.md](BASELINE-20261125.md). Earlier patch files and results remain available at tag `s31-rmt-v1.1.0`.

Base: BSP a308b54; Linux 7fdbad0c5; Buildroot external de2f5a6.
Do not replace the known-good full flash image or merge into integration until hardware validation.

The 2026-11-24 Linux runtime confirmed 2 online CPUs, Sv32, 14408 KiB MemTotal, successful 1 MiB tmpfs write/hash, and execution of a cross-compiled RV32 musl ELF with fork/wait and CLOCK_MONOTONIC. Only lo was exposed; the clock started in 1970.

The current device tree enables Ethernet, USB, I2C and SD/MMC; their reset provider is disabled in the baseline kernel config. Pinctrl also depends on that provider. This hypothesis was confirmed on the board: enabling the reset provider allowed pinctrl, GPIO, Ethernet, USB host, I2C and MMC to probe.

Apply the numbered Linux patches in `patches/linux/` in order to Linux 7fdbad0c5 before configuring. Append `configs/esp32s31-peripherals-experimental.config` (absolute path) to the existing Buildroot `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`, preserving both board fragments. Use a separate output directory or a backed-up baseline. Check the final kernel .config after Kconfig dependency resolution. Keep the known-good SPL/PSRAM patch and the same flash layout. Check image sizes against slot limits before flashing.

Validation order:
1. Boot and verify UART, both CPUs, memory, and repeat the native ELF smoke test.
2. Inspect deferred probes and driver bindings for reset, pinctrl, GPIO, I2C, Ethernet, MMC and USB. Mount debugfs temporarily if necessary.
3. Verify IPv4 loopback, then Ethernet link/DHCP with a cable. Do not infer physical link from driver binding alone.
4. Verify SocketCAN virtual interface via `ip link add dev vcan0 type vcan` (requires iproute2 with CAN support), then CAN frames using can-utils or a native test. Virtual CAN does not validate the physical CAN FD controller.
5. Test GPIO via character device; RGB LED requires the appropriate addressable-LED timing/driver. Test USB and SD/MMC with actual peripherals.
6. Check available memory, boot warnings and repeated reboot stability. Wall-clock synchronization and physical CAN FD remain separate work.

Rollback: retain the full known-good image with SHA-256 recorded in README.md. Restore only if necessary; no flash write is required for this branch setup.

## Hardware validation — 2026-11-25

- Kernel 6.18.0+ booted with 2 CPUs, 14376 KiB RAM total and 10500 KiB available at idle.
- Kernel and DTB flash writes verified by esptool. Bootloader and rootfs unchanged.
- IPv4 loopback ping passed; eth0 and YT8531 PHY initialized, IEEE 1588 hardware timestamp support reported. No physical carrier or DHCP tested.
- GPIOchip0 (62 lines), USB root hub, I2C adapter and MMC host appeared. External peripherals not tested.
- vcan0 created and brought up; frame exchange and physical CAN FD remain untested.
- WS2812 on GPIO60: RMT TX0 driver registered via Linux multicolor LED class; RGB writes succeeded without TX timeout. Physical red/green/blue transitions and smooth spectral fade confirmed by the board operator.
- Fade example: `examples/esp32s31/rgb-fade.c`, compiled for rv32imac/ilp32, transferred via UART to /tmp and executed with approximately 50 updates/s. Not persistent after reboot.
- Linux patch: `patches/linux/0001-esp32s31-rmt-ws2812-experiment.patch` (kernel commit d4f9210f0, parent 7fdbad0c5). Apply before enabling the config fragment.
- RMT v1.0 was an experimental single-pixel TX implementation; v1.1 supersedes it with the generic TX controller described in `RMT-v1.1.0.md`.
- Original known-good full flash image preserved. Existing nonfatal FDT reserved-memory warning remains.

### Reproduction

Build the patched XIP kernel and board DTB with the existing board and generic NOR test fragments plus the experimental fragment. Apply the generic NOR overlay to the new board DTB before flashing (it defines the CramFS rootfs window). Flash DTB at 0x300000 and xipImage at 0x500000, keeping SPL, U-Boot and rootfs intact. Validate sizes against the existing layout and keep rollback artifacts.

Validated RMT kernel SHA-256: `cecc8705d9cd84c4af06bc4a8607b0cf5b92a39728ec1870aae1ae6208f9bee5`. DTB SHA-256: `96dff5d495ef186b8d985d93a84ad1cfd663e27eb2ccec3ea1944145edf091f7` (before esptool padding).

Run the example cross-compiled with `riscv64-esp-linux-musl-gcc -march=rv32imac -mabi=ilp32 -Os -Wall -Wextra rgb-fade.c -o rgb-fade`. On the board: `./rgb-fade`. SIGTERM or SIGINT switches the LED off.

## RMT v1.1.0

See [RMT v1.1.0 release notes](RMT-v1.1.0.md) for the current validated kernel/DTB hashes, regression results and remaining limitations. The hashes above refer to v1.0.
