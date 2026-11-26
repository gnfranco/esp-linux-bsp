# S31 native radio: architecture and metadata-only proof of concept

Date: 2026-11-26. Status: experimental, no RF initialization or image changes.

## Decision and scope

Retain the Espressif BSP as the sole operational platform. Preserve S31 Lab
v1 at BSP commit `41c19d72577dd04e3e5d23077deb5d9a7510c808` as rollback.
Do not transplant a community radio image, its loader, binaries or flash map.
The community work remains a useful independently attributed reference.
No second SoC and no permanent reservation of one Linux hart are assumed.

The first experiment calls three side-effect-free functions from official
ESP-IDF binary archives in an independent freestanding Linux userspace ELF,
not inside the Linux kernel. It establishes a very narrow native execution
boundary, with no libc, no fake RTOS functions, no RF startup, and no MMIO.
It is not a Wi-Fi or Bluetooth driver or a complete ILP32F runtime.

## Verified upstream state

On 2026-11-26, queried actual remote refs and PR discussions:
- espressif/linux integration/v6.18-esp32s31:
  `7fdbad0c589c791cd2a6bc73fa9fcec8dbce4b11`.
- espressif/u-boot integration/v2024.07-esp32s31:
  `e5dff8d374ae3f68c1a3a09346be0cfb17b7df60`.
- espressif/opensbi integration/v1.6-esp32s31:
  `5395e03e1cb295257142cfdeaade6334f1543bcb`.
- espressif/esp-linux-bsp integration/v1.0-esp32s31:
  `9ecea5e6785b95270f6b21d7ce7dc769f9c7f81c` (already an ancestor of Lab v1).
- [Linux #1](https://github.com/espressif/linux/pull/1),
  [Linux #2](https://github.com/espressif/linux/pull/2),
  [U-Boot #1](https://github.com/espressif/u-boot/pull/1),
  [community U-Boot #1](https://github.com/GrieferPig/u-boot-esp32-s31/pull/1),
  [community Linux #3](https://github.com/GrieferPig/linux-esp32-s31/pull/3),
  [community firmware #1](https://github.com/GrieferPig/esp32-s31-linux-firmware/pull/1):
  all open, no maintainer responses in retrieved discussions; author follow-ups
  include PCM and concurrent associated WLAN tests. No new PR required yet.

The installed IDF v6.1 tag resolves to
`fff9895c82d744c7237be8847347bdd1b07c6643`. All nine selected radio archives
were fetched independently from Espressif Git repositories in Linvora-dev,
pinned to the exact IDF gitlinks and SHA-256 in
[the lock](../../dependencies/s31-native-radio.lock.json).
No community radio source or blob is used in this experiment.
The RV32 cross compiler already installed on Linvora-dev comes from the
community crosstool-NG release; the test is independent of a community BSP,
not yet of every community development tool.

IDF v6.1.1 exists at `051e4c4eb20346e043cdd47bc388fde75ff8af63`.
All four radio repository pins changed (Wi-Fi `ec08f0cee472`, PHY
`20f1db053a0e`, coexistence `c758e7b56e0`, BT `461f4cbeb67c`).
The release/v6.1 tip was `c43d665a96c0d90ea4740fa3ba12aaf89e48e568`.
The metadata test uses v6.1 as the controlled starting point; re-audit
v6.1.1 with a separate lock and compare before upgrading. Wi-Fi OSI version
9 and coex OSI version 2 were unchanged in the v6.1.1 headers observed.

## Architecture evidence and candidates

Primary source references (pinned official revisions):
- [Wi-Fi S31 adapter](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_wifi/esp32s31/esp_adapter.c):
  OSI callback table v9, IRQ routing/critical sections, semaphores, recursive
  mutexes, queues, event groups, tasks, timers, heap/DMA-capable memory,
  random, logs, NVS, clock/reset, PHY and coexistence.
- [Wi-Fi initialization](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_wifi/src/wifi_init.c):
  power domain and optional coexistence setup around the internal API;
  cannot merely call the vendor archive initialization without its environment.
- [Wi-Fi internal data interface](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_wifi/include/esp_private/wifi.h):
  internal TX copies caller buffer; RX callback handoff and explicit release
  of RX buffers require ownership/lifetime rules. The public control APIs
  include mode, scan and connect. This supports investigating a FullMAC
  Linux frontend; it does not prove that the private data/event ABI is
  complete or safe in Linux.
- [Coex adapter](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/esp32s31/esp_coex_adapter.c):
  OSI v2, IRQ-aware semaphores, high-resolution timers, internal allocation,
  crystal clock and coexistence scheduling.
- [BT controller](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/bt/controller/esp32s31/bt.c):
  OSAL pools, external/LP clock initialization, tasks, BLE/BR-EDR stacks
  and HCI transport. Target standard Linux HCI, not a second host stack.
- [ESP-Hosted-Linux architecture](https://github.com/espressif/esp-hosted-linux/blob/968f6dc7171465825efce4efba1b9015ecd5dd07/docs/architecture/overview.md)
  and [ESP-Hosted-MCU architecture](https://github.com/espressif/esp-hosted-mcu/blob/a93becf4b2d9e014e694b663760fc9866b290239/docs/architecture.md):
  reference control/data planes, frame ownership, cfg80211/netdev, HCI,
  capability negotiation and independent radio services; conventional deployment
  is another SoC connected over a transport. S31 support as a hosted target
  does not mean a same-SoC Linux backend exists.

**Preferred investigation:** a versioned, isolated radio service using IDF
radio libraries and bounded shared-memory control/data queues, with a minimal
Linux-owned broker for IRQ, clocks/resets, DMA-safe buffers and privileged
operations. Linux continues to own both CPUs, networking and BlueZ. Study a
cfg80211/netdev FullMAC frontend first because IDF implements scan/association
and data APIs; compare with SoftMAC only if lower-level stable frame APIs and
security ownership make it justified. Linux owns policy; do not duplicate state
machines, crypto or network stacks unnecessarily. FullMAC is a working
hypothesis, not a decision to implement before completion of resource/ABI gates.

Alternative: an in-kernel OSI port may reduce copies/latency but creates a
large trusted shim, nontrivial FPU/preemption/IRQ constraints, and licensing
and symbol boundaries. Do not link proprietary/prebuilt IDF archives into a
GPL kernel until license compatibility is independently established. A distinct
firmware/RTOS runtime on a hart or in M-mode would require a justified
interrupt/MMU/cache isolation scheme and likely compromises SMP; not selected
as default. ESP-Hosted transport semantics may inspire a local transport, not
a fabricated SPI/USB link to the same chip. Never expose unrestricted /dev/mem
or arbitrary DMA pointers as an expedient driver.

## Critical gate: FPU support, before radio initialization

The whole-archive static audit covers nine archives: 493 ELF32 RISC-V members,
all RVC + single-float ABI. It observed 9,873 unique global defined symbols,
5,935 unique strong undefined symbols, and 382 names not defined within this
selected set (not a final link closure). Examples include ROM function tables,
Wi-Fi state pointers, coexistence parameters, allocator and timer state.
Callback tables also hide dependencies that nm alone cannot reveal.

Floating-point mnemonic occurrences in disassembly: libpp 226; libnet80211
76; libble_app 20. Zero occurrences in the other six selected archives.
These are static counts, not proof all are on a relevant execution path.

The [IDF S31 SOC caps](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/soc/esp32s31/include/soc/soc_caps.h)
declare FPU and the EXT_ILL CSR FLW/FSW reason erratum; IDF
[FreeRTOS port](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/freertos/FreeRTOS-Kernel/portable/riscv/portasm.S)
uses single-precision register save/restore. The tested Linux DTS advertises
rv32imac_zicsr_zifencei, no F or D, although CONFIG_FPU=y. Linux's
[riscv_ext_f_validate](https://github.com/espressif/linux/blob/7fdbad0c589c791cd2a6bc73fa9fcec8dbce4b11/arch/riscv/kernel/cpufeature.c)
rejects F without D; the
[FPU save/restore](https://github.com/espressif/linux/blob/7fdbad0c589c791cd2a6bc73fa9fcec8dbce4b11/arch/riscv/kernel/fpu.S)
uses double-precision fld/fsd. Do not advertise F or D to work around this,
or execute arbitrary FP instructions on the baseline. Investigate whether
S31 is F-only, actual hardware/OpenSBI state, standard RISC-V architectural
FP versus vendor coprocessor semantics, context save/restore on both cores,
signal frames/ptrace and instruction trap behavior. A focused experimental
kernel branch and independent RFC may be warranted; no proposed patch yet.

The metadata getters used here are integer/pointer-only leaf code even
though the ELF advertises ILP32F. The actual linked instructions were reviewed
and no floating-point op is executed. Passing this probe does **not** qualify
the FPU, switching, interrupt correctness, PHY calibration or radio firmware.

## Memory / flash constraints

Existing physical PSRAM: 0x50000000–0x50ffffff (16 MiB).
The Device Tree reserves 0x2f030000–0x2f06ffff (256 KiB internal DMA pool,
no-map). Do not claim this pool for RF. Kernel XIP physical base 0x40400000;
rootfs aperture 0x40b00000 (4 MiB). These are physical mappings, not NOR offsets.

| NOR slot | Offset | Capacity | v1 size |
|---|---:|---:|---:|
| SPL | 0x002000 | 1,040,384 | 39,040 |
| FIT | 0x100000 | 2,097,152 | 259,432 |
| DTB | 0x300000 | 2,097,152 | 10,592 |
| Kernel XIP | 0x500000 | 7,340,032 | 4,294,412 |
| Rootfs | 0xc00000 | 4,194,304 | 3,502,080 |

Kernel slot headroom 3,045,620 bytes and rootfs headroom 692,224 bytes are
not RF allocation permissions. Measure ROM/IRAM/SRAM symbols, DMA alignment
and addresses, shared clock ownership, flash XIP/cache mapping, actual
relocation size, stack/heap peak and remaining memory before selecting a
partition. No slot layout changes in this experiment. Baseline smoke tests
reported approximately 10 MiB MemAvailable, but this does not establish the
internal SRAM budget for real-time radio and DMA buffers.

## Reproduce the metadata-only test

Obtain the official repositories at the full pins in the lock (sparse
checkout esp32s31 for Wi-Fi/PHY/coex; root files for BT). Keep these outside
the source tree; do not redistribute archives or the linked executable.
The Wi-Fi, PHY and coexistence repositories explicitly describe Apache 2.0
object-form licensing; BT contains NimBLE NOTICE and requires a separate
complete licensing review. Source-only experiment; no binary release.

```sh
CC="$HOME/toolchains/s31-linux-gcc-15.2.0-5/bin/riscv32-esp-linux-musl-gcc"
D="$HOME/deps/s31-radio"
python3 experiments/s31-native-radio/audit.py "$D" "$CC" > /tmp/s31-radio-audit.json
experiments/s31-native-radio/build-probe.sh "$D" "$CC" /tmp/s31-probe-build
# Check SHA-256 and disassembly; relay only the tiny test ELF privately to the
# authorized UART station. Never flash. Requires target's S31 Lab v1 profile.
python3 experiments/s31-native-radio/run-uart.py --port /dev/ttyUSB0 \
  --artifact /tmp/s31-probe-build/abi_probe.elf \
  --sha256 86d2c56ba48a061b3d46975a82bffa58811865d74679259109fb64526f74bf17
```

## Evidence

Build: GCC 15.2.0, RV32IMAFC, ILP32F, static freestanding with gc-sections,
no relax, no undefined symbols or PT_INTERP. ELF 2,076 bytes, RX segment 756
bytes, no writable program segment. Independent rebuild was byte-identical.
Size is only the metadata probe, not the required radio payload.
Three linked library getters have been inspected in disassembly (no MMIO,
calls to hardware, or FP instructions). Actual hardware result via UART:

```text
riscv32
6.18.0+
0-1
mtd:rootfs / cramfs ro,relatime 0 0
MemAvailable: 10332 kB
TARGET_SHA256_PASS 86d2c56ba48a061b3d46975a82bffa58811865d74679259109fb64526f74bf17
COEX=2.0.0
BTDM=39ed4dc
PHY_CAL_VERSION=100
S31_RADIO_ABI_PASS
0-1
ETH0_PRESENT
MemAvailable: 10020 kB
S31_RADIO_ABI_HARDWARE_PASS
TARGET_ARTIFACT_REMOVED
```

No new flash, no modified baseline, no actual Wi-Fi/BT operation. Presence
of eth0 is not traffic validation; MemAvailable difference is not a measured
leak. Full logs and artifact retained privately on respective hosts.

## Ordered gates for next iteration

1. Linux FPU/ABI investigation and context integrity, with independent
   experimental kernel worktree. Validate each hart, process switching,
   signals, ptrace and reboot; retain v1 rollback. Compare v6.1.1 dependencies.
2. Resolve archive-selected call graph, ROM table/ABI and MMIO dependencies
   using official symbols, source and linker map; reject unimplemented services
   rather than stub success. Map interrupt sources, internal memory and clocks.
3. Implement small, testable OSI primitives (queues, timed waits, critical
   sections, memory liveness) without touching RF registers. Define typed
   bounded broker, teardown and recovery. Test concurrency and resource limits.
4. Only then attempt staged clocks/reset/PHY initialization with bounded
   timeout, rollback, regulatory defaults and no arbitrary raw transmission.
5. Passive scan, WPA2-CCMP association, DHCP/IP, HCI, BLE/GATT, BR/EDR,
   A2DP PCM, coexistence and regression of other peripherals. Real differential
   CAN bus and physical audio remain separate gates. Publish no production
   image unless adequately validated.
