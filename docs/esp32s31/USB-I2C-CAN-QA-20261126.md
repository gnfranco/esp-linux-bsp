# ESP32-S31 peripheral validation — 2026-11-26

Hardware: ESP32-S31-Function-CoreBoard-1 rev 0.0, 16 MiB NOR, 16 MiB PSRAM.
BSP base: `241817dc9e0fad2bc41998f9b113249872d96b4d` (integration/v1.0-esp32s31).
Linux source base: `a8fe3e328154415b04a6960171add04347034c25` (validated RMT series).
Experimental Linux tip: `9b41c9c5df88ed883b05662187e431bb3452e491`.
The nine patches in `patches/experimental/s31-canfd/` replay to Linux tree
`f179d36a9a2bf63733fd4d0567d174c82e96c545`, verified with an independent Git index.
This branch is a test profile, not an upstream-approved release.

## Results

| Test | Actual result |
| --- | --- |
| USB 2.0 HS host | Kingston DataTraveler mass storage enumerated at 480 Mbit/s, block device and four partitions detected |
| USB block reads | 262144 bytes, two identical copies, no mount or disk write; repeated in the final regression |
| I2C0 /dev/i2c-0 | ES8311 at 7-bit address 0x18, chip ID 0x8311, version 0x01 |
| I2C combined transactions | 128 repetitions of the three identification registers: 384/384 transfers, repeated on final kernel |
| SocketCAN virtual | 128 classic frames, 128 FD frames, verified identifiers and data, zero RX/TX errors |
| Physical controller probe | can0 at 0x20394000 and can1 at 0x20395000, actual CTU CAN FD driver |
| Classical CAN isolated self-test | 128/128 frames per controller, 8 bytes, 250 kbit/s nominal |
| CAN FD isolated self-test with no BRS | 128/128 frames per controller, 64 bytes |
| CAN FD isolated self-test, BRS 250/500/1000/2000 kbit/s | 128/128 frames for each rate on each controller, 64 bytes |
| Final regression | 128 classic + 128 FD BRS at 1 Mbit/s per controller; no RX/TX errors or drops |

FD matrix total: 1280 verified 64-byte frames (5 scenarios x 2 controllers x 128 frames), excluding the additional 512-frame mixed final regression. Userspace disabled the sending socket's local echo and checked all data, standard/extended identifiers and BRS flags. The drivers' cumulative RX includes a controller state notification per interface startup; matching data frames were verified independently. The GPIO Matrix routes TX and RX through the same unused pad per controller (GPIO35/36). Both interfaces were stopped after tests. This proves the real controller datapath, not a differential ISO 11898-2 physical bus.

The USB second read can be served from cache. Negotiated USB link speed is not a throughput measurement; no file system was mounted, and the pre-existing alternate GPT warning was left untouched.
The I2C test only writes a register address pointer followed by a read, and does not change codec configuration or validate I2S/audio output.
Final Linux 6.18.0+ booted with two CPUs and CramFS read-only; approximately 10240 KiB MemAvailable during tests.

## Driver integration and regression findings

ESP-IDF rev `08e0d30a` register definitions identify the S31 TWAI block as CTU CAN FD (ID 0xCAFD, IP version 2.4), which Linux already supports. Reuse its existing driver; append clock/reset DT binding IDs without renumbering, enable APB/core clocks (XTAL source) and reset, describe both nodes in the SoC DTS, and use an explicitly selected lab-only loopback overlay. The board's normal DT does not enable or assign external CAN pins.

Three existing upstream fixes were backported with author attribution:
- [PRESUME_ACK/self test, c31a435](https://github.com/torvalds/linux/commit/c31a435933f18be0f874302161333e9f16e200a0)
- [SSP selection above 1 Mbit/s, e707c59](https://github.com/torvalds/linux/commit/e707c591a139d1bfa4ddc83036fc820ca006a140)
- [Bus error interrupt acknowledgement, e74bae8](https://github.com/torvalds/linux/commit/e74bae899529f49c0f375307983d12e8ecad7d4b)

Initial controller probe succeeded; attempting to send without TX/RX feedback timed out. Same-pad loopback plus the self-test fix passed classical and FD without BRS. BRS failed at 250, 500 and 1000 kbit/s on both controllers, while 2 Mbit/s passed when the old driver happened to select the disabled SSP encoding. Enabling bus error reporting exposed an already-fixed stuck BEI interrupt.
The final profile incorporates the existing upstream fixes and an additional experimental one-line correction to explicitly disable SSP at <=1 Mbit/s (encoding 1, not 0, which selects measured delay + zero offset). All ten FD scenarios and the final mixed regression passed. This last fix is a candidate for upstream review, not a claimed accepted contribution. See CTU CAN FD IP Core Datasheet section 3.1.46:
https://canbus.pages.fel.cvut.cz/ctucanfd_ip_core/doc/Datasheet.pdf

## Reproduce

Apply the active RMT series first, then the nine experimental CAN patches, in ascending order, to the pinned Linux base. Append both new config fragments after the existing experimental peripherals fragment; resolve Kconfig dependencies. Build XIP kernel and DTBs with the same compiler/config/overlay as the validated baseline. Apply the existing generic NOR rootfs overlay followed by the test-only CAN loopback overlay to the board DTB. Do not connect anything to GPIO35/36 when using this overlay. Do not replace known-good firmware until sizes and rollback images are checked. Test only on an isolated bench.

Final tested kernel SHA-256: `5fe191d8e189a229f68848791bdb22b851b1b1922ca3f4b0a150030ba0f406ed`.
Self-test board DTB SHA-256: `87c8fe870ac985f707aed9974f649d3486ceae12ae987bc597d84a8ce0126106`.
The DTB is flashed at 0x300000 and the kernel at 0x500000. Writes and separate flash verification passed. SPL, FIT and read-only rootfs were unchanged. The source was built before the final explanatory commits, so recompilation can change build metadata.
The smoke test tools are in `examples/esp32s31/peripheral-qa`. The binding passed dt-doc-validate and the patch series replayed byte-for-byte at source-tree level. No stress/throughput, electrical waveform, full bus interoperability or suspend/resume tests were performed.

## Remaining gates for all-in-one

- Add separate TX/RX pinmux assignments and two CAN FD transceivers for an isolated differential bus, terminate at both ends, verify bidirectional traffic and real ACK/error recovery.
- Test additional USB classes/filesystems if desired; storage read/write tests should use disposable media.
- Integrate the independently tested radio profile (Wi-Fi and Bluetooth), LED/RMT and validated vendor boot/PSRAM/reset changes into a single reproducible image; verify flash size and memory headroom, and repeat all peripheral/radio regressions together.
- Validate the audio path through the I2S codec/physical output separately. I2C register reads do not prove audio.
