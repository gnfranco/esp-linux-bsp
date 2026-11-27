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

First A boot: Linux started, both harts and CramFS confirmed;
DWC2 created the high-speed device, but setting configuration #1
failed with error -32 before storage enumeration. The original
storage-required profile guard rejected this boot, so no counter
snapshot or media read was accepted. This is an observed failure
in the instrumented/no-relax baseline, not proof of an alignment
cause. The collector was extended in c8d658d to preserve boot/idle
counters even when the expected USB block device is absent, and
only perform read tests after the exact known medium is identified.

The diagnostic kernels are larger than the original XIP. A
sector-rounded 4,620,288-byte restoration span was generated from
the original release full image at offset 0x500000 (first
4,294,412 bytes exact original XIP, remaining 325,876 bytes all
0xff), SHA-256
03813e4ff39cf7dee15334f454ec327f5c306d789fd0d6065fc2b101e9dd01e7.
The subsequent hardware script verifies and restores the whole
modified span, in addition to verifying the five release components.

## Matched diagnostic hardware result

The second A boot enumerated Kingston and completed the read test;
B enumerated and passed on its measured boot. Both were verified
against the original DTB, exact config, and same command line.
Collector c8d658d (SHA-256
 e1af9ddfc9470e444bbf29977f0ac58a656bfe292339b32c778f6d9948298c31)
collected boot/idle/read snapshots with distinct boot IDs per run.
All snapshots had zero lost events for CPUs 0 and 1, and no
saturated or decreasing counters. No USB-media mounts/writes.

Boot USB selected-path counts (sum of both CPUs):

| Direction | Payload/transfer bytes | A direct | A align_ok | B direct | B align_ok |
| --- | --- | ---: | ---: | ---: | ---: |
| from | 0-16 | 4 | 0 | 0 | 4 |
| from | 17-32 | 4 | 0 | 0 | 4 |
| from | 33-64 | 2 | 0 | 2 | 0 |
| from | 128+ | 19 | 0 | 19 | 0 |
| to | 0-16 | 13 | 0 | 13 | 0 |

No forced/address-selected bounce or failure or DWC2 host-URB
aligned-temporary-buffer event was observed in these measured
snapshots. No emac or other-class streaming-map event was
reported; no Ethernet gateway available. This is not a proof
that all other DMA paths are unused.

Idle delta: no events in either run. Read phase: 537 A and
588 B USB from/128+ direct maps; different counts despite the
same 1 MiB workload, not a throughput comparison. No additional
alignment bounces. All eight 128 KiB hashes matched A, B, and
the previously qualified reference. Aggregate SWIOTLB 256 slots,
idle usage zero, boot and post-read hiwater 0 in A and 1 in B.

This attributes the previously observed SWIOTLB high-water
behavior to eight USB DMA_FROM_DEVICE mappings selected for
alignment bounce in B (four 0–16 B, four 17–32 B), rather than
force/address selection or the recorded DWC2 URB temporary
allocation path. A chose direct for those same size/direction
bins. It demonstrates the intended size-based mapping decision
and preserved read integrity under the matched instrumented
builds. Does not prove the cause of the previous Oops or of the
first A USB configuration failure, nor timing equivalence with
the exact production-like candidate. No claim of independent
trials or definitive fault cure.

Final rollback: complete modified kernel span digest matched,
all five release component digests matched, original boot profile
passed, UART free. Private Orion archive
~/source/build/s31-dma-path-qa-20261127/dma-path-evidence.tar.gz,
44,939 bytes, SHA-256
bfd3eb2a66ccb43dae9763884d30fc92266eb5cab7b888b738a1de52fef4e48b.
No raw logs or media hash samples published.

Readiness: an independent narrow alignment RFC can summarize
this selected-path evidence and the 42/42 candidate reboot screen,
with precise caveats. Keep the diagnostic commits outside the
candidate patch. Ethernet traffic and physical CAN controller
loopback on the alternate DTB are incomplete in this regression;
repeat with an authorized connected link and confirmed disconnected
CAN matrix pads. External ISO 11898-2 and the independent DMA32
and firmware cache questions remain outside this patch's claims.

## Ethernet cable-connected regression

On 2026-11-27 the operator connected Ethernet. The original
S31 Lab v1 and the exact non-instrumented candidate 047a032d
both brought eth0 up, obtained IPv4 through the local bench
network, and completed 10/10 ICMP echo exchanges, 1,000-byte
payload, to the configured local gateway. No addresses or
credentials recorded here. This is a functional Ethernet
regression, not a throughput or extended load qualification.
The earlier no-carrier situation was due to bench connectivity,
not a demonstrated defect in either kernel.

Only the XIP kernel was changed; the original DTB was retained.
Following the candidate test, all five original flash-component
digests matched and the original boot profile passed, UART free.
Private Orion evidence:
source/build/s31-dma-regression-20261127/ethernet-connected/
ethernet-connected-evidence.tar.gz, 9,137 bytes, SHA-256
f11d4b8021f34dd7a7f427893c8093d264a6ab1800030c01205fe0f8be53e793.

Physical CAN controller loopback remains outside this follow-up
regression unless the matrix pads GPIO35/GPIO36 are confirmed
unconnected. Previous controller and external-bus limitations
remain as documented; virtual SocketCAN is not SSP validation.
