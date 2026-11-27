# ESP32-S31 native radio: memory-resource inventory and admission gate

Date: 2026-11-27. Scope: static inspection of the original Lab v1
artifacts and official ESP-IDF v6.1 source. No radio initialization,
new allocation, MMIO access, kernel modification, flash write or
hardware test. Keep Lab v1 intact.

## Pinned primary sources

ESP-IDF v6.1 commit fff9895c82d744c7237be8847347bdd1b07c6643:

- [S31 SoC address map](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/soc/esp32s31/include/soc/soc.h),
  blob e32604a5f4c931b0701fd10e402693386f88c3d4: 512 KiB unified D/IRAM,
  64 MiB external-memory aperture and ROM stack constants.
- [S31 heap layout](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/heap/port/esp32s31/memory_layout.c),
  blob 90040ea191a533265807a22f42fa598eda53588f: internal RAM has DMA
  capability; SPIRAM is a different heap class; reserve ROM area from
  SOC_ROM_STACK_START minus SOC_ROM_STACK_SIZE to end of D/IRAM.
  IDF also reserves image data/IRAM and optional LP/ULP resources.
- [Heap capability definitions](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/heap/include/esp_heap_caps.h),
  blob 235b02ca9940288373056db21f98b11f4c3d6305.
- [S31 coex adapter](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/esp32s31/esp_coex_adapter.c),
  blob ec8eddb75cdb0ef645fbb456e1ad8dec1a7b1555: internal allocator requests
  8BIT | DMA | INTERNAL.
- [S31 Wi-Fi adapter](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_wifi/esp32s31/esp_adapter.c),
  blob 48b2ba9dc31037f74b22917664423321a16af8d2: internal realloc/calloc/zalloc
  request the same combination; static Wi-Fi queue storage requests
  internal byte-accessible RAM; generic allocation and queue allocation
  may prefer PSRAM depending on configuration. Do not equate these classes.
- BT controller source inspected at the same pin (blob
  c8fc8f4f863938b08f2b3b557b13b22a85728e36); the simple heap-capability
  search yielded no direct matching lines. BT pool sizing and allocator
  dependencies are still unresolved.

Lab v1 Linux commit 9b41c9c5df88ed883b05662187e431bb3452e491:
[SoC DTS](https://github.com/gnfranco/linux/blob/9b41c9c5df88ed883b05662187e431bb3452e491/arch/riscv/boot/dts/espressif/esp32s31.dtsi),
[kernel/dma/direct.c](https://github.com/gnfranco/linux/blob/9b41c9c5df88ed883b05662187e431bb3452e491/kernel/dma/direct.c).
The global coherent DMA pool is a shared resource for noncoherent devices
when the configured DMA path selects it. Ethernet stmmac and USB xHCI have
coherent allocations; actual usage must be measured, not inferred from calls.
The combined F-only/DMA config inspected has CONFIG_DMA_GLOBAL_POOL=y,
CONFIG_SWIOTLB=y and CONFIG_ESP32S31_CACHE=y.

## Artifact-based inventory

Original rollback directory (private): ~/deps/s31-research/lab-v1-rollback/images.
All SHA256SUMS entries passed. The auditor's test suite also independently
pins the exact standard and CAN-test DTBs and the FIT. The two DTBs have the
same memory and DMA layout.

All intervals below are half-open, in physical address space.

| Region | Start | End (exclusive) | Bytes |
|---|---|---|---:|
| HP internal SRAM | 0x2f000000 | 0x2f080000 | 524288 |
| OpenSBI file extent only | 0x2f000000 | 0x2f020b2c | 133932 |
| Linux default DMA pool | 0x2f030000 | 0x2f070000 | 262144 |
| IDF ROM reserved interval | 0x2f07afb0 | 0x2f080000 | 20560 |
| Linux PSRAM | 0x50000000 | 0x51000000 | 16777216 |

The FIT default configuration is conf-1, with firmware opensbi.
OpenSBI has a file-backed load extent only: BSS, scratch, stacks, firmware
domain protection and any other runtime claims are not determined by the
file length. No current pool usage is established. The 64 MiB PSRAM
address aperture is not the installed 16 MiB. The IDF ROM interval is a
source-derived reservation, not newly measured hardware ownership.

The remaining geometric intervals are NOT free SRAM. No part of the pool
or unqualified space is assigned to radio. RADIO_ALLOCATABLE_BYTES=UNKNOWN.

## Reproducible read-only auditor

Source: experiments/s31-native-radio/resources/memory-audit.c,
build-memory-audit.sh and test-memory-audit.py. Host-only, no kernel rebuild.
Reuses host libfdt from existing Linux checkout 060679ff35be74800f0fcb792296e01e5ff524ee.
The kernel's libfdt subset declares but does not implement fdt_check_full;
the first build failed to link it. The corrected build uses bounded file
length, version and header checks plus checked traversal. This tool is
intended only for trusted, hash-verified build artifacts, not arbitrary
untrusted input, a generic DT validator or a boot admission authority.

```sh
R=experiments/s31-native-radio/resources
B="$HOME/deps/s31-research/lab-v1-rollback/images"
DTC="$HOME/deps/s31-research/native-radio-next-20261127/f-only-dma-object/scripts/dtc/dtc"
"$R/build-memory-audit.sh" "$HOME/projects/linux-s31" /tmp/s31-resource-audit
(cd "$B" && sha256sum -c SHA256SUMS)
/tmp/s31-resource-audit/memory-audit "$B/esp32s31-function-core-board-1.dtb" "$B/u-boot.itb"
python3 "$R/test-memory-audit.py" /tmp/s31-resource-audit/memory-audit "$DTC" "$B"
```

Codex CLI read-only review found three gaps: non-exact Lab pool/PSRAM
sizes, not checking the selected default FIT firmware, and Python asserts
that disappeared with -O. Corrected in build-03. Both DTBs and a controlled
positive fixture passed; 14 negative fixtures were rejected, including
overlap with OpenSBI/ROM/reserved memory, duplicate or missing pool,
misalignment, missing no-map, reusable pool, dynamic reservation,
RV32 range overflow, undersized pool/PSRAM, wrong selected firmware and
truncated FIT payload. The complete suite passed both normal Python and -O.
Exact positive output lines and artifact digests are tested.

The FIT check validates its default configuration and external payload
bounds, not every possible bootloader configuration choice or the
live firmware footprint. This is static verification, not bench validation.

## Architecture decision and next gates

Keep generic userspace allocations distinct from internal/DMA allocations.
Do not implement the vendor allocator with malloc as a fallback. Linux
must retain ownership of DMA buffers and any hardware-specific resource.
The existing global DMA pool is not an exclusive radio arena.
Before allocating for radio, recover exact OpenSBI runtime regions and
ROM/firmware ownership, measure existing pool occupancy under peripheral
load, and quantify vendor allocation patterns including BT and PHY.
Then design a bounded, driver-owned interface with explicit lifetime and
failure reporting, and qualify it against the existing peripherals.
Do not expose unrestricted physical memory or alter the flash/DTB layout.
