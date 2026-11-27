# S31 Lab v1: early DMA alignment experiment

Date: 2026-11-26 (operator local date); build started 2026-11-27 UTC.

Status: experimental; source, full build and controlled physical
boot screens verified. Not a causal fix claim or production release.
See [DMA path instrumentation and subsequent peripheral regression](DMA-PATH-REGRESSION-20261127.md).

## Baseline and rationale

See [matched boot diagnostics](BOOT-DEBUG-LIST-MATCHED-20261126.md)
for the complete USB authorization, hotplug and reboot controls.
Without the pendrive, 12/12 controlled reboots passed. With it
present and automatic USB authorization disabled, two of six trials
had early Oops even before mass-storage binding. Normal USB
authorization passed six of six in that matched run. These small
samples neither prove that USB causes memory corruption nor exclude
physical power or timing effects.

Code review identified an initialization-order inconsistency:
`setup_arch()` calls `riscv_set_dma_cache_alignment()` before the
S31 cache driver's `arch_initcall` registers vendor noncoherent
cache operations. In that interval, the DMA cache alignment can
be reduced to 1 even on a noncoherent system. The later registration
does not restore it. This affects slab alignment and the
size-based bounce decision for small DMA buffers. The release
enables `CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC`.

The proposed experiment retains the initial `ARCH_DMA_MINALIGN`
(64 bytes in this release configuration) specifically when the
S31 cache driver is configured and the root compatible matches
`esp,esp32s31`. The existing cache registration and SBI probe are
unchanged. No change to the DWC2 driver, SMP, OpenSBI, DTB, CramFS
or flash partitioning is proposed. This does not establish that
DMA is the cause of the observed Oops, or that the retained
alignment is sufficient for all S31 DMA masters.

## Pinned experiment

- Linux base: `9b41c9c5df88ed883b05662187e431bb3452e491`
- Linux branch: `experiment/s31-dma-alignment`
- Candidate commit: `047a032ddaeb1b6c8354b6fa5aeb0ffe48db0b89`
- Changed file: `arch/riscv/mm/dma-noncoherent.c` (13 additions)
- Compiler: Espressif riscv64-esp-linux-musl-gcc 14.1.1
  (esp-linux-14.1.1_20260922)
- Configuration: `configs/esp32s31-lab-v1.kernel.config`, unchanged
  after olddefconfig (identical SHA-256):
  `71c1077bb6cd8f07a091db0ffd0e7f5bda36ad954cc5bc74bd29960def4f8b38`
- SMP, XIP, S31 cache driver, SLUB_TINY, DWC2 and DMA bounce remain enabled.
- `git diff --check` and checkpatch on format-patch: pass;
  checkpatch 0 errors, 0 warnings.
- `arch/riscv/mm/dma-noncoherent.o`: built successfully,
  ELF32 RISC-V relocatable, soft-float ABI.
- Full `make ARCH=riscv CROSS_COMPILE=... -j1 xipImage dtbs`:
  started in isolated out-of-tree build; results pending at time of
  writing.

The candidate source and build are isolated on Linvora-dev in
`~/worktrees/linux-s31-dma-alignment` and
`~/deps/s31-research/kernel-v1-dma-alignment-build`. The latter
contains configure, object and full-build logs, config comparison,
status record, and (only upon successful build) image checksums.
Existing branches/worktrees and release artifacts remain intact.

## Bench acceptance gates (not yet executed)

1. Confirm UART availability, original release component hashes,
   the candidate XIP slot limit, and rollback artifact integrity.
2. Verify successful candidate boot, the early retained-alignment
   diagnostic and later vendor cache registration. Confirm the
   original matched two-hart profile and read-only rootfs.
3. Compare baseline and candidate using the same pendrive, power
   supply, reset method, command line, DTB, rootfs, settle time and
   observation window. Use balanced blocks with the same trial
   count; record baseline non-reproduction separately.
4. Record fresh boot IDs, USB enumeration, Oops/panics, list warnings,
   DMA/cache diagnostics and warm reboot results. Test hotplug and
   read-only storage access; no mounting or writes. Measure supply
   voltage/current if equipment is available; no inference of
   stable power from an absent brownout indication.
5. Regress Ethernet, USB, I2C, GPIO/RMT, CAN and reboot. Restore
   release v1 and verify component hashes unless specifically
   qualifying a new image.

A passing small sample is not proof of causality. Do not submit this
SoC-specific experiment as a generic RISC-V fix until the mechanism
and hardware results are validated and the maintainers' preferred
architecture is established.

## Independent source review

Codex CLI 0.172.0, gpt-6.2-astra (high), reviewed the actual
candidate without editing the source. The review confirmed that
the Device Tree is unflattened before the alignment decision
and found no correctness defect in the added compatibility guard.
This is source review, not hardware qualification.

With the existing SWIOTLB pool, retaining 64 bytes changes the
eligibility for bouncing small noncoherent receive and bidirectional
mappings; coherent and transmit-only mappings bypass this test.
The original release already logged a 512 KiB SWIOTLB allocation.
Verify pool size, usage and exhaustion with the candidate rather
than attributing any result to the creation of a new pool.

A separate RV32 concern was identified in `arch/riscv/mm/init.c`:
the expression `4UL * SZ_1G` in the DMA32 physical limit calculation
wraps to zero on RV32. This can cause the SWIOTLB allocation path
even when no DMA32 address bouncing is necessary. Do not change
that expression in this experiment; investigate it on a separate
branch after checking upstream history and DMA address-limit
semantics.

Firmware cache-maintenance granularity and invalidate behavior
remain unverified, even if the existing SBI writeback probe passes.
Capture actual small receive-buffer bounce behavior and memory
pressure; check the vendor cache registration and verify a
non-S31 DT control before proposing any general RISC-V change.

## Complete candidate build (2026-11-27 UTC)

The isolated Linvora-dev build of `xipImage dtbs` completed
successfully (22m20s, exit 0), using the pinned exact v1 config.

- XIP: 4,294,412 bytes
- XIP SHA-256:
  `a4ba9b2a004da5c407f0eba5cdbddf6bc4eafc250b91179329099538f7091338`
- Private bench transport archive SHA-256:
  `bf2dcdeed086877be7b54e7472ba58de7616a408a38416052dc6510c1dd8c013`
- All seven constituent archive digests verified on Orion.
- The rebuilt DTB differs from the release DTB; it is not used
  in the bench test. Only the kernel slot changes.

Disk maintenance removed only the clean, redundant DEBUG_LIST
worktree, retaining its branch and commit. The older two build
directories retain original configurations, XIP images, symbol
files, logs, and archived compiler command/dependency files;
all recorded image/config digests and archive hashes verified.
The candidate build, original release rollback, and all toolchains
remain intact. Approximate free disk increased from 3.2 to 5.5 GiB.

## Matched physical screening with USB present (2026-11-27 UTC)

The Kingston DataTraveler 3.0 (62 GB) remained connected during
the entire A/B/A screen. All five original flash component
digests passed before the first control and before candidate
installation. The candidate changed only the XIP slot at
0x500000; the original release DTB was verified again.

Both controls used the same release command line:
`earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128`.
The runner required RV32 no-F, both harts online, read-only CramFS,
fresh boot IDs after resets, the vendor cache registration message,
and Kingston enumeration, with a 2 s USB settle in the profile and
3 s between trials. It additionally required the 64-byte alignment
diagnostic for the candidate and its absence in the original.
ROM/RTS-assisted resets are not physical power cycles.

| Phase | Warm software reboot | ROM/RTS-assisted reset |
| --- | --- | --- |
| A: original v1 | 5 PASS, 1 KERNEL_OOPS | 6 PASS |
| B: 047a032d | 6 PASS | 6 PASS |
| A: rollback v1 (separate recovery control) | 1 KERNEL_PANIC | Recovery Oops, then separate recovery PASS |

The first A phase recorded 14 UART captures (initial, 12 trials,
one recovery); 13 confirmed profiles with distinct boot IDs.
Warm trial 6 failed with an Oops at 2.292600 s; the first reported
access fault was at 2.269969 s, about 2.783 ms after the USB
mass-storage detection at 2.267186 s. CPU0, init PID38,
EPC c00a1de2 (vma_last_pgoff), RA c00a1eaa
(__anon_vma_interval_tree_augment_propagate+0x20), bad address
00000004, load page fault. ROM/RTS recovery passed.

The candidate's initial boot and all 12 trials passed; 13 UART
captures, 13 distinct confirmed boot IDs, and no Oops/panic/list
corruption signature found. Every profile confirmed
`ESP32-S31: retaining 64-byte DMA cache alignment`,
the later vendor cache registration and Kingston enumeration.
The SWIOTLB mapped range remained 0x50f58000–0x50fd8000
(512 KiB) in both revisions.

### Read-only USB integrity

On a separate post-screen boot in each phase, the runner verified
the correct RV32 two-hart CramFS profile, Kingston vendor/model,
and absent USB mount. Four 64 KiB reads from /dev/sda at block
offsets 0, 16, 256 and 1024 (4096-byte blocks) completed, with
16+0 records in/out for each. All four SHA-256 digests matched
between baseline and candidate; 256 KiB per phase. No medium
mount or write was performed. This checks sampled data integrity,
not all USB workloads or the specific DMA bounce mechanism.

### Rollback and recurrence

Only the original XIP slot was restored, then all five release
components verified again. The restored baseline's initial boot
passed; its first warm reboot panicked. The initial access fault
at 2.244248 s occurred *before* USB mass-storage detection at
2.247576 s, followed by Oops on CPU0 init PID38:
EPC c00a1de2, RA c00a1eaa, bad address 00000004. A second
Oops on CPU1 init PID1 at EPC c00a1de2, RA c00a1f26
(__anon_vma_interval_tree_augment_rotate+0x12), bad address
00000004 led to panic. The automatic ROM/RTS recovery itself
had an Oops in anon_vma_clone+0xb8 (EPC c00af0e0, bad address
0000011e), so the recovery screen correctly exited 94.
The separate, single read-only ROM/RTS recovery then passed
with original v1, both harts online, Kingston enumerated and UART
released. No further flash writes were made.

The A/B/A result strengthens the hypothesis but is not proof of
causality or production qualification; trials are small, sequential,
and no supply voltage/current or cache maintenance granularity
was measured. The second failed A recovery is not counted as
an independent matched trial. The observed fault even before
mass-storage detection reinforces that application-level storage
I/O is not a prerequisite. More balanced blocks and direct
DMA-bounce evidence, plus peripheral regression testing, remain
necessary before upstream submission.

Raw UART evidence and runner are retained privately on Orion:
`~/source/build/s31-dma-alignment-20261127/evidence.tar.gz`
(75,316 bytes, 70 archive entries).
SHA-256 `e0aaffc32933c2c9edae2fcfb19ed9a5f2c8474bbf2d6b3a0cc3a4804853a1a2`.
Raw traces were not published; the transfer to Linvora-dev of
this evidence archive has not been confirmed. Candidate binary,
config, symbols and build logs are already on Linvora-dev.

## Further qualification gates and upstream separation (2026-11-27 UTC)

Official Espressif Linux integration remained at
`7fdbad0c589c791cd2a6bc73fa9fcec8dbce4b11`; the existing RMT,
CAN and U-Boot RFCs, and the three community RFCs, remained open
with no maintainer review at the time of inspection. Do not
combine these contributions or resubmit their existing changes.

An extended A/B/B/A screen was started with 12 warm reboots
per block (24 per revision, identical v1 DTB and config).
The first A block passed 12/12, illustrating baseline
intermittency. The remaining blocks and restoration must be
reported as actually completed rather than assumed.

The v1 config enables DEBUG_FS, SWIOTLB and
DMA_BOUNCE_UNALIGNED_KMALLOC, but not FTRACE, DMA_API_DEBUG
or dynamic debug. The existing SWIOTLB debugfs interface
provides `io_tlb_nslabs`, `io_tlb_used` and
`io_tlb_used_hiwater` (units are slots, not bytes).
A read-only diagnostic harness,
`experiments/s31-reboot/swiotlb-screen.py`, records these
aggregate counters before and after eight 128 KiB USB reads,
with matching content digests. It has passed Python syntax
compilation; hardware qualification is pending. The counters
are not attribution of specific DWC2 mappings or bounce reasons.
If needed, measure the small noncoherent RX/bidirectional bounce
decision with a separate opt-in diagnostic build; do not mix
instrumentation into the release-like A/B/B/A experiment.

The official ESP-IDF v6.2 S31 checkout
`08e0d30a74ad0bfd5a34933142b80f45619ee410` corroborates
64-byte L1 D-cache lines in
`components/esp_system/port/soc/esp32s31/Kconfig.cache` and
`components/hal/esp32s31/include/hal/cache_ll.h`.
This substantiates the chosen alignment but does not verify
effective firmware cache synchronization correctness.

A separate firmware risk needs evaluation. The official S31 ROM
capability header sets ESP_ROM_CACHE_WRITEBACK_NEEDS_SYNC_TWICE_MAP;
the corresponding IDF patch
`components/esp_rom/patches/esp_rom_cache_writeback_esp32p4_esp32s31.c`
implements the vendor's double-sync workaround. The pinned
Lab v1 OpenSBI commit
`0940d8a5834d02b93e2be0e60ef6505767862875` instead calls the
ROM cache routines directly in
`platform/generic/espressif/esp32s31/cache_mmu.c`.
Assess applicability to the actual silicon and the shared cache
controller's SMP serialization, and preserve the vendor's
authorship/license if adapting its workaround. No firmware change
was made or tested here; this is not an asserted cause.

Mainline Linux master at
`9437ee8b6cf607e883b36e44bd755968daaebd48` still contains
`dma32_phys_limit = min(4UL * SZ_1G, (unsigned long)PFN_PHYS(max_low_pfn))`.
Upstream `cfca5a48b03fbd33c8cb84cb73ee2e34467f3a33`
(Troy Mitchell, "riscv: mm: fix SWIOTLB initialization for systems
with DRAM above 4GB") addresses a different case while changing
the SWIOTLB initialization condition. The RV32 unsigned-long
wrap remains a separate investigation, including phys_addr_t
width, exclusive address-limit semantics, memory-size cases,
and the interaction with existing stable backports. Do not
claim authorship of the above-4GB fix or change pool sizing
as part of the alignment experiment.

The compile-only RV32/RV64 proof is retained as
`experiments/s31-reboot/dma32-width.c`. Espressif GCC 14.1.1
compiled both variants with -Wall -Wextra -Werror. The RV32
function returns zero; RV64 returns 0x51000000 for S31 RAM.
The actual candidate ELF independently confirms the RV32
zero store in paging_init at c0005b44. This proves the
arithmetic behavior, not an upstream regression or a
general-purpose fix.

Codex CLI's independent source review confirmed the absence
of event tracing in the Lab v1 config, the linked SWIOTLB
debugfs handlers, and a second consumer of the runtime DMA
alignment in DWC2's temporary aligned URB buffer allocation.
For attribution, compare opt-in counters for successful
alignment-selected direct DMA bounces with address-limited,
forced and failed mappings, and DWC2 temporary buffer
allocation behavior. Record directions and size buckets;
avoid per-map logging, keep diagnostic builds identical except
for alignment, and do not claim that a large userspace read
proves a particular small mapping bounced. Firmware ROM status
handling, granularity and writeback synchronization are
separate prerequisites.

## Extended A/B/B/A matched screen (2026-11-27 UTC)

All four blocks ran to completion on Orion. Each used 12 warm
reboots, same Kingston medium, same original DTB, same kernel
configuration and command line, 2 s USB settle and 3 s between
trials. Two recovery attempts at most were allowed after an
unsuccessful trial; recovery outcomes are not matched trials.
Only the XIP slot changed; all five original components
verified after restore and UART was released.

| Block | Warm passes/trials | Median passing time (s) | MemAvailable (KiB) |
| --- | --- | ---: | --- |
| A1 original | 12/12 | 10.68 | 9796–10088 |
| B1 candidate | 12/12 | 10.59 | 9940–10092 |
| B2 candidate | 12/12 | 10.61 | 9888–10180 |
| A2 original restored | 11/12 | 10.57 | 9864–10120 |

A2 warm trial 4 again had an Oops, with first access fault
at 2.251558 s (before mass-storage detection at 2.264894 s),
CPU0 PID39, EPC c00a229e
(anon_vma_interval_tree_remove+0x12), RA c00b32c2
(anon_vma_interval_tree_pre_update_vma+0x2e), bad address
00000030. The ROM/RTS recovery passed.

Each block had 13 confirmed profiles with distinct boot IDs.
There were 26 B UART captures, with no Oops, panic or list
corruption signature; 27 A captures, of which the above
was the only fault capture. The two matched matrices together
yield original 28/30 warm and 6/6 ROM/RTS, candidate 30/30
warm and 6/6 ROM/RTS. Extraordinary rollback recovery errors
from the earlier screen are excluded from this pooled count.
This is supportive but not a demonstrated causal fix, and
the trials are not necessarily statistically independent.

Private Orion archive:
`~/source/build/s31-dma-long-screen-20261127/long-screen-evidence.tar.gz`,
76,608 bytes, SHA-256
`d9d90615769dd8a78ad5e16f2acdea516bdc8552a63fb8be3d030f3da7fec411`.
Raw logs not published.

## Physical SWIOTLB counter and 1 MiB integrity check

After the extended screen, the original and exact same candidate
were tested independently using the new diagnostic script, whose
SHA-256 is 84a521c7b2ffbf41347ad2c9ef4f5b3171d28e4c2fc2160b8cda0a3555417bff.
Original DTB, command line, both harts and Kingston medium were
preserved, with read-only debugfs mount only if needed (unmounted
on completion). Eight 128 KiB reads at the same block offsets
completed in each phase; all eight content digests matched.
No USB mounting or writes.

| Counter (slots) | Original before/after | Candidate before/after |
| --- | --- | --- |
| io_tlb_nslabs | 256 / 256 | 256 / 256 |
| io_tlb_used | 0 / 0 | 0 / 0 |
| io_tlb_used_hiwater | 0 / 0 | 1 / 1 |

All eight per-read samples had the same respective peak values.
One slot is 2 KiB. The candidate had already used a SWIOTLB
slot before the read workload; the original had not recorded
any use. This directly confirms different aggregate bounce-pool
activity, while retaining equal capacity and idle use. It does
not attribute the bounce to DWC2, size, direction, alignment
reason, or prove that it prevented an Oops. Only one measurement
per revision was made; no high-water reset was performed.

The original XIP was restored again, all five components
verified, and final original boot/profile passed. UART free.
Private Orion archive:
`~/source/build/s31-dma-long-screen-20261127/swiotlb-evidence.tar.gz`
(14,559 bytes), SHA-256
`ef571cfc32fd5958ae159251e6e654a3ad86f94d9ac63a50c4dc445e73a13460`.
Raw logs and sampled media-content digests are not published.

### Readiness decision

The narrow alignment change is supported by source ordering,
official 64-byte line documentation, full exact-config build,
repeated controlled physical screens, rollback recurrence,
and aggregate pool telemetry. Before an upstream RFC, prefer
a small matched opt-in diagnostic for the successful
alignment-selected bounce path (device, direction and size
bucket), using identical instrumentation in A/B, and verify
DWC2's temporary buffer path and firmware cache behavior.
Regress Ethernet traffic, USB data, I2C, GPIO/RMT, CAN in the
authorized available configurations, and reboot. A subsequent
RFC can state the initialization-order bug with appropriately
limited hardware evidence, without claiming a definitive cure
for the intermittent failures or bundling the DMA32 or firmware
investigations.
