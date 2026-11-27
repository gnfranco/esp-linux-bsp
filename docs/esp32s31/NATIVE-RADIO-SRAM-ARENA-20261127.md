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

## Physical qualification result

Full kernel build completed with status 0. Kernel image 4298508 bytes,
SHA-256 b938c056b4b8d699c9c64db4d893f17888091e61da0da79ddc118635ed956211.
The compiled DTB matches the previous statistics build; the original
Lab v1 DTB remained on the board. The experimental kernel was transferred
through a private draft release in gnfranco/esp-linux-lab.

Before flashing, all five original image digests matched. Because the
candidate is 4096 bytes larger than the original kernel, backed up the
entire affected erase-aligned flash span (4300800 bytes from 0x500000)
from the physical board. Original kernel prefix matched the Lab v1 file.
Backup SHA-256 8a0ef4de94b59d407cfbf668ae4625d2be9ec0d6372dcab4906e9a5ebecc0e3b.
Candidate kernel alone written and independently verified.

Physical result: ARENA_HARDWARE_PASS. Kernel emitted exactly
s31-dma-arena: PASS bytes=16384 floor_pages=48.
All suballocation, alignment, pattern, zeroing, oversize denial
and release checks passed. Post-proof pool snapshot:
page_bytes=4096, total_pages=64, used_pages=3, free_pages=61,
tracked_pages=3, peak_pages=4, largest_allocatable_pages=32,
allocation_failures=0. Both harts online, Ethernet interface present,
CramFS root read-only, no screened kernel fault, debugfs unmounted.
No radio startup, no bus-master DMA experiment, no userspace mapping.
This is a temporary kernel CPU-access SRAM ownership proof, not a
persistent radio backend. No full traffic regression was performed.

Rollback restored and verified the entire backed-up span, then all
five original slots. Restored Lab v1 boot and both harts confirmed,
eth0 present, candidate marker absent. RESTORED_RUNTIME_PASS and
UART_FREE; no residual experimental kernel in the flash slot.

Private evidence on Linvora-dev:
~/deps/s31-research/s31-dma-arena-proof-20261127/physical-evidence/
candidate-evidence.tar.gz SHA-256
8c882c888da7145e96b7fcde03982bc05de480c6a51ac2d37d663c87813274fe
restored-evidence.tar.gz SHA-256
d05223ebfd3b23d7e889edc94cab3a1b3c59a2c6bc27ae9ba7e31f1280637e76
Both archives independently hash checked. Exact flash backup remains
on Orion, alongside the session script and raw logs.

Next gate: design a persistent arena with explicit Linux device
identity and safe lifetime/access semantics, then establish actual
radio DMA reachability before connecting the allocation callbacks.
Retain the headroom policy as experimental, not a guaranteed budget.
