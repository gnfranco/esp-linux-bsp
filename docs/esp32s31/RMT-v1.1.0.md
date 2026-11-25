# ESP32-S31 RMT driver v1.1.0 (experimental)

> Historical record. For the current patch series use [BASELINE-20261125.md](BASELINE-20261125.md). Earlier patch files and results remain available at tag `s31-rmt-v1.1.0`.

Hardware validation: 2026-11-25, Function-CoreBoard-1, silicon rev 0.0. Linux 6.18.0+, base 7fdbad0c5, v1.0 patch d4f9210f0, v1.1 patch 205d569fd. The BSP integration branch retains both patches in order.

## Changes

- Separate generic RMT TX controller (`drivers/soc/esp32s31-rmt.c`) and WS2812 LED class consumer.
- Four exclusive TX channels, one 48-word hardware block per channel including EOF; per-channel mutex and completion; shared IRQ state protected by spinlock.
- Clock/reset providers now expose the RMT APB gate, XTAL-based functional clock and reset. RMT memory power still uses a syscon regmap.
- IRQ completion replaces polling on normal TX; timeout and hardware error paths stop the channel.
- Separate YAML bindings for controller and WS2812 child; GPIO60 routes TX0 via pinctrl. Existing LED sysfs name and fade application unchanged.

## Validation

- Kernel and DTB compiled successfully and flashed with verified hashes; SPL/U-Boot/rootfs unchanged. Prior v1.0 images retained locally for rollback.
- Boot: 24 clocks registered, RMT IRQ 15 (interrupt matrix source 22), 40 MHz reference, four TX channels initialized, TX0 LED registered.
- RGB primary writes succeeded. IRQ count increased from 1 to 5 during initial color test; fade regression increased from 7 to 219 over five seconds (212 IRQs), no reported TX errors. Operator confirmed smooth physical fade again.
- IPv4 loopback ping 2/2, eth0 present, 10436 KiB available RAM at idle.
- `checkpatch.pl`: 0 errors, 5 warnings (MAINTAINERS, binding patch separation, Kconfig help). `git diff --check` clean. YAML parsed and DTB compiled; dt-schema semantic validation and sparse not run (tools unavailable).
- TX1–TX3 are implemented but not exercised physically or for concurrency. RX, DMA, streaming beyond 47 symbols, suspend/resume and upstream review remain pending. WS2812 consumer is single-pixel. Existing reserved-memory warning remains. Not a production or upstream-approved driver.

## Reproduce

Apply numbered Linux patches 0001 and 0002 to Linux 7fdbad0c5 and append `configs/esp32s31-peripherals-experimental.config` to the board and generic NOR config fragments. Build XIP kernel and board DTB; apply the generic NOR overlay to preserve the CramFS rootfs node. Validate the layout before flashing. DTB offset 0x300000; kernel offset 0x500000.

Validated image SHA-256 (before esptool padding):

- xipImage (4130572 bytes): `7382a937d77ad2ed928460f35ac8f6f3636652e23728756520d22baa7821fd70`
- board.dtb (10126 bytes): `7eacf0c48b512956ed9634204a1105842e54dd86b4634804f08d22afb3fdbb35`

Run the unchanged `examples/esp32s31/rgb-fade.c` cross-compiled for rv32imac/ilp32. The demo runs in RAM and is not installed into the rootfs. Restore the prior verified v1.0 DTB and kernel together if rollback is needed.
