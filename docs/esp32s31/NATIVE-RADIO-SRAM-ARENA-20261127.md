# Kernel-owned SRAM arena proof — development milestone

Date: 2026-11-27. Kernel source experiment/s31-dma-arena-proof,
commit 39066a9cadae76bf4da50867d362975d55f48960, based on the
previously qualified global-pool statistics kernel 7a542131.
No change to the original Lab v1 image or device tree.

## Implementation and policy

Optional S31_DMA_ARENA_PROOF (default n), depends on ARCH_ESPRESSIF,
OF and DMA_GLOBAL_POOL_STATS. Dedicated qualification image only.
One-shot late initcall, exact machine compatible esp,esp32s31,
4 KiB page size and global pool geometry (64 pages starting at
0x2f030000). Uses existing coherent allocator, with a CPU-only path
that skips device address translation. Existing device callers pass
zero reserve floor and still obtain their translated DMA handles.

Reserve exactly four pages (16 KiB) and require 48 free pages
(192 KiB) AFTER this acquisition. The policy check and allocation
are serialized by the existing pool spinlock. The floor is a chosen
temporary experimental limit, not a radio budget or a permanent
reservation for other consumers. Other allocations can change
occupancy after acquisition. A fragmented pool can deny the
request even with enough total free pages. Impossible-floor
negative control exercises the guard before the real acquisition.

Use gen_pool with 64-byte granularity for two independent aligned
requests: 1600 bytes at 64-byte alignment, 500 bytes at 256-byte
alignment (charged 512). Check initial zeroing, alignment,
non-overlap, charged bytes, distinct pattern readback, rejection
of an oversized request while blocks are live, and full subpool
release. Scrub and read back the acquired extent before returning
it to the global pool. Memory barriers order WC writes before
readback and release. No fake DMA device, no bus address exported,
no MMIO radio access, no userspace mapping, no RF. The proof does
not establish actual radio DMA reachability or concurrent hardware
ownership. No persistent arena is retained.

## Verification to date

Astra high read-only review confirmed the locked floor, preserved
ordinary allocation path and ownership model. Identified missing
barrier in unexpected success of impossible-floor probe; corrected
before final cross compilation. No second independent review.
checkpatch: 0 errors, 0 warnings; git diff --check passed.
Final coherent.o built for RV32 using GCC 14.1.1:
35cf02be055ccdb18ad7514afe53c4fb9aeaae8e67012f3f7d56d2847c4b7a8f.
Compared to the instrumented baseline config, only
S31_DMA_ARENA_PROOF=y added. Kernel source branch clean and pushed.

Full xipImage/dtbs build launched in separate output directory
~/deps/s31-research/s31-dma-arena-proof-20261127/build, PID 89760.
Not yet complete at this checkpoint. Check full-build.status,
full-build.log and FULL-SHA256SUMS before using any output.
Original build artifacts untouched.

## Next bench gate

Verify full build, candidate size within the existing kernel slot,
DTB unchanged and all rollback hashes; only then use established
temporary kernel-only flash and restore procedure. Require exactly
one explicit s31-dma-arena: PASS bytes=16384 floor_pages=48 log,
no FAIL or SKIP, both harts online, pool accounting consistent and
successful cleanup/reboot. A successful boot alone is not a pass.
Full peripheral regression deferred per project decision.
The next architectural work is a persistent Linux-owned arena
with explicit device identity, allocation lifecycle, bounded
mapping interface and DMA quiescence. It must not be wired to the
radio OSI callbacks until ownership, backing and access attributes
have been established. No claim of completed physical backend.
