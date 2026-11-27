# S31 global coherent DMA pool: experimental statistics

Date: 2026-11-27. Follow-up to NATIVE-RADIO-MEMORY-AUDIT-20261127.md.
Linux branch experiment/s31-global-dma-pool-stats; commit
7a542131c806aaae330579a9955ff728506e15b1; based directly on original
Lab v1 Linux commit 9b41c9c5df88ed883b05662187e431bb3452e491.
No new allocation from the coherent pool, DTB change or RF startup.

## Instrumentation

Optional CONFIG_DMA_GLOBAL_POOL_STATS depends on DMA_GLOBAL_POOL and
DEBUG_FS. The debugfs file dma_global_pool_stats (mode 0400) reports
aggregate page counts, no physical addresses or device identifiers.
The counters update under the existing coherent allocator spinlock;
allocations are charged the actual power-of-two page extent.
The same shared helper also maintains private per-device pool counters
when enabled, but only the global pool is exposed. Alloc failures
count failed attempts inside the coherent allocator, not all DMA API
failures. The counts are diagnostic, not a stable ABI.

A snapshot bitmap is allocated in ordinary kernel memory, before taking
the pool lock; the bitmap and counters are copied together under the
lock. Counting and the aligned power-of-two search run after unlocking.
No DMA pool allocation is requested by the reader. A read may fail
with ENOMEM. Stats do not provide a reservation or guarantee subsequent
allocation success. Peak and failures are cumulative since pool init.
The reported largest extent concerns the bitmap allocator only, not
device-specific DMA mask, hardware access, or radio ownership.

Expected fields after boot (values intentionally unknown):
page_bytes, total_pages, used_pages, free_pages, tracked_pages,
peak_pages, largest_allocatable_pages, allocation_failures.

## Reproduction and review

The initial broad GPT-6.2 Astra xhigh architecture review reached its
95-second timeout without a final report. No findings attributed to it.
A focused GPT-6.2 Astra high read-only review completed in 26 seconds:
confirmed accounting and extent alignment, flagged unnecessary IRQ-off
latency while scanning and noted shared-helper instrumentation of
device-specific pools. Corrected by scanning a private bitmap snapshot
outside the spinlock. The review preceded this correction; the amended
code was compiled and checkpatch-checked, not independently re-reviewed.

Initial configuration failed because flex was missing from PATH;
reused the existing host tools, without installing packages. Build from
the kernel branch above, with a new output directory:

```sh
export ARCH=riscv
export CROSS_COMPILE="$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-"
export PATH="$HOME/deps/s31-host-tools/root/usr/bin:$PATH"
export BISON_PKGDATADIR="$HOME/deps/s31-host-tools/root/usr/share/bison"
O=/tmp/s31-dma-stats-build
mkdir "$O"
cp "$HOME/worktrees/s31-native-radio/configs/esp32s31-lab-v1.kernel.config" "$O/.config"
scripts/config --file "$O/.config" --enable DMA_GLOBAL_POOL_STATS
make O="$O" olddefconfig
make O="$O" -j2 kernel/dma/coherent.o
```

Final object build succeeded; SHA-256:
7d71e541e1ba33b02161acd6d589a19bc5718330e4453abbcff31e1e1405d697.
scripts/diffconfig reported only DMA_GLOBAL_POOL_STATS=y added to the
Lab v1 config. git diff --check and checkpatch: zero errors/warnings.
Private evidence: ~/deps/s31-research/global-dma-pool-stats-20261127/
(build/, config.log, build-reviewed.log, codex-astra-review.log).
The source-only patch is on the fork; no upstream PR.

## Physical qualification pending

No linked kernel or new image has been constructed; no physical
measurements have been taken. Preserve the existing rollback. Build
an experimental image with only the instrumentation change and check
the exact config, size and hashes before authorized bench use.
On the target, mount debugfs if needed and read
/sys/kernel/debug/dma_global_pool_stats as root. Capture boot-id
correlated snapshots at idle, under Ethernet traffic, USB read-only
load and concurrent Ethernet/USB. Do not write to the USB medium.
Verify total_pages=64 and page_bytes=4096 for the current DTB,
used_pages+free_pages=total_pages, tracked_pages=used_pages,
peak_pages>=used_pages and largest_allocatable_pages<=free_pages.
The largest extent must be zero or a power of two; peak and failure
counts cannot decrease during one boot. Compare peripheral throughput,
error counts, SMP and software reboot with uninstrumented Lab v1.
Do not infer failure-free admission or reserve radio resources from
a single idle snapshot. Restore the original image afterward.
OpenSBI runtime footprint and radio/PHY allocation requirements
remain unresolved.
