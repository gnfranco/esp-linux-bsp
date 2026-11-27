# S31 Lab v1: early DMA alignment experiment

Date: 2026-11-26 (operator local date); build started 2026-11-27 UTC.

Status: experimental; source and object compilation verified. No
candidate boot, physical qualification, or production release yet.

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
