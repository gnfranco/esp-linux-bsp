# ESP32-S31 UART register compatibility audit

2026-11-27. Static source inspection following intermittent console
response failures under USB/Ethernet load, including the independent
IP liveness observation documented in GLOBAL-DMA-POOL-BENCH-20261127.md.
No UART kernel modification, no change to the original board DTB.

## Exact source basis

Lab v1 Linux commit 9b41c9c5df88ed883b05662187e431bb3452e491.
The same UART source is present in the DMA instrumentation candidate.
Git blobs:
- drivers/tty/serial/esp32_uart.c: 11c8601eba7b586b4ba3414c0995eaf593cd61a0.
- arch/riscv/boot/dts/espressif/esp32s31.dtsi: a323d1d3ee1006968420cda6e0a18c4019862d27.
- drivers/clk/clk-esp32s31.c: 1df3a9c8a8f9d1043aca28dfa344865edb6370fa.

ESP-IDF v6.1 commit fff9895c82d744c7237be8847347bdd1b07c6643:
- [uart_reg.h](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/soc/esp32s31/register/soc/uart_reg.h)
  blob 10a5dfabe0f8fc47598069dae7fd7a1c93adbca6.
- [uart_ll.h](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_hal_uart/esp32s31/include/hal/uart_ll.h)
  blob 381fb94f5d720247659c68294041b153a7b1cee2.
- [hp_sys_clkrst_reg.h](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/soc/esp32s31/register/soc/hp_sys_clkrst_reg.h)
  blob 1235ea780e78678e678c812d2a252a6d9090448e.

Source files retrieved via GitHub contents API at the pinned commit,
decoded and Git blob hashes checked. Private copies and review in
~/deps/s31-research/global-dma-pool-stats-20261127/.
Source-only audit, no vendored headers in the repository.

## Confirmed mismatches

The S31 SoC DTS selects esp,esp32s3-uart and the xtal fixed-clock
phandle for UART0. The board DTS enables UART0. The binding currently
has only esp32 and esp32s3 variants. This does not model S31 differences.

| Field / register | Linux path selected by S31 DT | Official S31 HP UART |
|---|---|---|
| RXFIFO count | STATUS bits 9:0 | bits 7:0 |
| TXFIFO count | STATUS bits 25:16 | bits 23:16 |
| TX empty threshold | CONF1 shift 10 | CONF1 shift 8, 8-bit field |
| RX flow enable | CONF1 bit 22 | HWFC_CONF_SYNC +0x2c bit 8 |
| TX flow enable definition | CONF0 bit 15 | CONF0_SYNC bit 13 |
| Loopback | CONF0 bit 14 | CONF0_SYNC bit 12 |
| Software RTS | CONF0 bit 6 | CONF0_SYNC bit 21 |
| Software DTR | CONF0 bit 7 | CONF1 bit 20 |
| RTS / DTR inversion | CONF0 bits 23 / 24 | CONF1 bits 18 / 19 |
| Clock config | UART +0x78 | UART +0x88 (TX/RX clock and reset bits) |
| Clock source/divider | S3 UART clock register fields | HP_SYS_CLKRST UART0_CTRL0 +0x88, source bits 6:5, integer divisor bits 15:8 |

The old UART +0x78 corresponds to UART_NEGPULSE_REG on S31.
CONF0 bit 6 is TXD_BRK, bit 14 IRDA_EN and bit 23 TXFIFO_RST
on S31. Existing esp32_uart_set_mctrl() manipulates the old modem
control bit positions. The official uart_ll_set_rts_active_level(),
uart_ll_set_dtr_active_level(), uart_ll_set_loop_back() and
uart_ll_set_hw_flow_ctrl() confirm different register ownership.
uart_ll_set_sclk() and uart_ll_set_baudrate() program HP_SYS_CLKRST;
XTAL selection is 0, not the S3 UART selector value 3. CLKDIV_SYNC
+0x14 retains the 12-bit integer and 4-bit fractional divider, and
REG_UPDATE +0x98 retains bit 0 synchronization. Do not copy the S3
clock setup to S31 or assume boot firmware clock state for all rates.
The clock provider already owns the HP_SYS_CLKRST syscon window, but
does not yet export UART clocks. Avoid a second unsynchronized raw
clock-control mapping in the UART driver.

## Additional RX-path review (Astra high, read-only)

- esp32_uart_rxint() ignores tty_insert_flip_char() failure and
  increments icount.rx, without accounting for dropped flip-buffer bytes.
- Startup enables RX-full and break only. The driver has no explicit
  RX FIFO overflow event accounting. IDF header confirms overflow is
  bit 4 in raw/status/enable/clear and RX timeout is bit 8.
- IRQ handler snapshots FIFO count once, handles TX, then clears the
  original status. Whether a newly arriving RX event is lost depends
  on hardware clear/reassert semantics; source inspection alone cannot
  prove this. Need a focused experiment.
- Break handling also merits review; not a diagnosis of the ASCII
  command corruption.

The source mismatch and accounting gaps are real. No causal link
between any specific mismatch and a missing UART character has been
established. Do not treat successful paced runs as proof of a kernel
fix, or a console timeout as proof of a kernel hang.

## Proposed independently reviewable work

1. Add native S31 compatible and variant layout with unit coverage;
   preserve original ESP32/S3 behavior and review all modem control,
   FIFO, early console and clock paths, including synchronization.
2. Model UART functional and bus clock ownership through the existing
   S31 clock provider, coordinate boot console handoff and resets.
   Avoid changing console rate or gating its clock without a fallback.
3. Separately add RX overflow/flip-insertion accounting and test
   drain/ack ordering against documented S31 behavior. No unconditional
   register resets in the IRQ handler.
4. Qualify boot output, bidirectional checked serial traffic at 115200,
   other supported baud rates, RX/TX under Ethernet and USB load, SMP,
   console recovery, reboot and rollback. Validate error counters.
5. Keep these changes separate from DMA pool instrumentation and
   the radio allocator. No external PR until validated.

This audit establishes a porting task, not a finished UART driver.

## Independent port-plan review

Astra high reviewed the audit, kernel clock provider and UART sources.
Recommended independent clock-setup and REG_UPDATE capabilities,
native binding/early console path, and preservation of firmware baud
until the clock provider can represent the live configuration.
Before clock or reset writes, verify bootloader parent, effective
rate, integer/fractional dividers, gate/reset state and recovery
console. HP_SYS_CLKRST and UART both have registers at offset 0x88
but in different MMIO windows. Keep functional and bus clock ownership
in the existing provider; do not make clock framework calls that may
sleep while holding port->lock, in IRQ or in console writes. Ensure
the console clock remains available through handoff and TTY close.
Bound register-update waits. Independently test overflow and software
buffer drop accounting. These are review recommendations, not
implemented or qualified driver changes.
