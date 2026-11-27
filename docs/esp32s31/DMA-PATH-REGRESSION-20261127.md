# S31 Lab v1 — DMA path instrumentation and peripheral regression

Date: 2026-11-27 UTC. Experimental diagnostics only.

## Exact candidate non-instrumented regression

Candidate: 047a032ddaeb1b6c8354b6fa5aeb0ffe48db0b89.
XIP SHA-256:
a4ba9b2a004da5c407f0eba5cdbddf6bc4eafc250b91179329099538f7091338
Configuration:
71c1077bb6cd8f07a091db0ffd0e7f5bda36ad954cc5bc74bd29960def4f8b38.
Default v1 DTB, both harts, CramFS read-only, same OpenSBI,
same command line and Kingston DataTraveler 3.0 device.
Each session verifies the original five flash components before
replacing only the kernel slot, and restores the original XIP and
verifies all five components afterwards.

- USB: enumeration at 480 Mbit/s. Eight 128 KiB reads (1 MiB
  total) matched all prior exact-media SHA-256 samples. No mount
  or write.
- I2C: ES8311 at 0x18, chip 0x8311, rev 0x01. 128 sets of three
  combined register-pointer/read transactions, 384/384.
- SocketCAN: 128/128 classic and 128/128 FD frames, standard
  and extended identifiers, checked flag/payload, zero RX/TX
  errors, on a temporary virtual interface removed after test.
  Physical CAN controllers disabled in default DTB; not retested
  on alternate DTB in this regression. No external CAN bus
  qualification is implied.
- RMT RGB: three colors/brightness sysfs write/readback and
  restore; Lab02 synthetic debounce 3 accepted/3 suppressed,
  zero drops, then 3 electrical BOOT/DTR GPIO61 pulses accepted,
  zero drops. The initial GPIO runner reported failure
  at final LED off regex (no recorded value). Repeated with an
  explicit target shell test and tagged readback; confirmed
  LED_OFF_READBACK=0. Synthetic intervals 494–504 ms slow and
  117–122 ms fast; physical stimulus 496–504 and 118–123 ms.
- Ethernet: eth0 present, initially administratively down. Both
  original and candidate tested after setting eth0 up and waiting
  up to ten seconds for carrier: no carrier on either. No DHCP
  or network traffic test result. Not a demonstrated regression,
  and not a qualification of Ethernet traffic.
- Reboot: 5/5 software and 1/1 ROM/RTS matched profiles. No
  selected Oops/panic/list corruption/USB I/O errors in the smoke
  test. Candidate cumulative 35/35 warm and 7/7 ROM/RTS across
  the prior and this screen (42/42), not a statistical proof of a
  causal fix. The trials are not necessarily independent.

Private logs remain on Orion under
source/build/s31-dma-regression-20261127. No sampled content
digests, addresses, UART transcripts or flash backups published.

## Opt-in instrumentation, separate from candidate

Baseline diagnostics branch:
experiment/s31-dma-diag-baseline
reviewed commit 9e7d859b1a60b11246506b24e9c17c27d898c3e6,
initial instrumentation 0db789c0c5bdc5ac06fa96c4cec60c65baa8b0cc.
CONFIG_S31_DMA_DIAGNOSTICS=y is the only config difference
from the exact candidate configuration; config SHA-256
ed712491bef6650f881985e3fb841b3902183d55b96024b81dae74bc832a9683.
Instrumented build and artifacts are in an independent output
directory; exact candidate artifact is untouched.

Counters are per CPU and fixed DT class (usb, emac, other).
The S31 USB and EMAC compatible strings identify the single
controller nodes; the other class aggregates remaining noncoherent
DMA devices, not individually identified. The code avoids mutable
device-name pointers (identified during read-only Codex CLI
review), no dynamic allocation, no per-map messages, no raw
addresses or payloads. Counters saturate at U32_MAX; collector
rejects saturation and nonzero lost count. Snapshot rows are
not globally atomic; collect after workloads quiesce.
The instrumentation can perturb timing.

Counts cover noncoherent, non-MMIO direct DMA mappings and
the DWC2 host URB temporary alignment-buffer allocation path.
Bounce reason is the selected route, not independent alignment
demand: force precedes addressability, which precedes the
kmalloc size/alignment predicate. Address and alignment fail
include no active pool or actual map error. Scatter-gather counts
are per attempted segment, including segments unwound on later
failure. The DWC2 allocation event is independent of successful
DMA mapping; size bins are URB payload lengths, not allocator
padding/pointer bytes. The separate DWC2 split-IN allocation is
not included. Coherent mappings and MMIO are excluded.

The reviewed change removed the dependency on
DMA_BOUNCE_UNALIGNED_KMALLOC so control builds retain diagnostics,
and explicitly guards diagnostic-only conditional branches. The
follow-up read-only Codex CLI review found no material issue;
this is source review, not physical validation.
Affected RV32 object builds and git diff --check passed;
checkpatch reviewed follow-up: 0 errors/0 warnings. Full patch
has MAINTAINERS/new-files warning, expected for lab-only source.
The shared collector rejects duplicate rows, unknown class/event,
nonzero lost, saturation, decreasing counters or a different
boot identity; checks USB read length and hashes; measures
boot, idle, and USB workload deltas plus SWIOTLB pool counts.
Parser synthetic self-tests passed.

Do not claim causation from this instrumentation. It is for
matched A/B selected-path evidence only, not intended to be part
of the upstream alignment fix.

All three physical regression sessions restored and verified the
five v1 flash components, then confirmed original boot and free UART.
Private Orion archive regression-evidence.tar.gz: 58,607 bytes,
SHA-256 18e71dc097e2cb7f9cfe61cc8cee366592b9f6e0f39ad5f87859a33a61e75809.

## Matched full builds

The first default-relaxation diagnostic link failed with
R_RISCV_RVC_JUMP relocation truncated against ns_capable_noaudit
in kernel/ptrace.o. That object had the exact same SHA-256 as
in the already-qualified candidate, suggesting a layout-sensitive
linker relaxation problem rather than a source change in ptrace.
The diagnostic A and B builds both succeeded with the existing
release-like config and explicitly matched vmlinux linker flags
'-z norelro --build-id=sha1 --no-relax'. This is a diagnostic-only
workaround; the uninstrumented candidate was not rebuilt or
changed. Do not infer timing equivalence to that candidate.

| Profile | Kernel commit | XIP bytes | XIP SHA-256 |
| --- | --- | ---: | --- |
| A baseline | 9e7d859b1a60b11246506b24e9c17c27d898c3e6 | 4619916 | e9d9d276d02ae76eef79bd5b203afa1b0a4f986ef7b5618f85e95d54192789c1 |
| B retained | c49cd5ce797fbb76fff01a7116bae85f01a30c0d | 4619916 | 0bedb065ffa146961b1f750181e459485956690dec7d021a821776047ebdda50 |

Both DTBs byte-identical to the candidate DTB, both configs
identical to each other, both XIPs fit the 7 MiB kernel slot.
The retained branch is the baseline diagnostic tree plus the
single candidate change in arch/riscv/mm/dma-noncoherent.c.
Built artifacts and full logs preserved in
~/deps/s31-research/s31-dma-diag-pair-20261127 on Linvora-dev.
The experiment/s31-dma-alignment worktree returned to its original
clean candidate branch and exact candidate artifact verified.
The two XIPs and pinned collector were transported using a
private draft, not a release; no raw hardware logs included.

On Orion, the initial hardware preflight rejected comparison of
the freshly built DTB (10,434 bytes; SHA-256
121ff20b8e36135032b7e21ee27673a20e6994490dd9890c2a382923c74a016e)
with the release DTB (10,592 bytes; SHA-256
090508d75505c389b915dd868b49c180650ec066876a803ae28394bc644eac42).
Decompilation with the existing kernel DTC showed only the
release's additional flash@40b00000 mtd-rom rootfs node.
Corrected the preflight to require each exact hash and byte-equal
A/B compiled DTBs, plus verification of the original release DTB
on flash. Only the XIP slot is replaced in either phase; no DTB
is flashed. No hardware change occurred during rejected preflight.
