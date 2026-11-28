# ESP32-S31 RCU stall investigation, 2026-11-27

## Observed failure

The earlier Wi-Fi OSI SRAM backend test produced all functional
markers with rc=0, but its overall qualification was incomplete.
The exact kernel was e328a231fcc71494777b014d77302b230e3c21ca,
XIP SHA-256 180a6041f4d7a785c99a8dc9e73b383b233cf22eceebb63b609dc7bfd5be4401.
At 2.361371 s USB mass storage was detected; at 23.407735 s,
before the test ELF ran, rcu_sched self-detected a stall on CPU0.
At 86.419623 s another report appeared during debugfs unmount,
which subsequently timed out. Both reports have g=-1007,
kworker/0:2 PID36, workqueue events, CPU0 softirq 196/196.
The elapsed grace-period ticks grew from 5250 to 21003 at HZ250.
This is evidence of one prolonged stalled grace period, not two
independent stalls. Both harts still reported online, the shell
executed commands and the allocation test passed. No proof that
both CPUs or all kernel work remained healthy.

Exact candidate vmlinux SHA-256
2d2e4face0ecc3955713e0e7fe01f528595888ee96c88887aa7907097c45a954.
First EPC c010d2ae -> kernfs_notify_workfn+0xb4, immediately before
kernfs_root(kn) in the loop over root->supers. First RA c010d2ec
follows ilookup. Second EPC c00da0da -> ilookup, RA c00da0d6,
caller c010d2e8 in kernfs_notify_workfn. Same kn register value
s0=c0a12f20 in both snapshots. The samples localize the executing
worker, but do not prove a cyclic list, corrupt inode chain, deadlock
or bug in RCU itself. No kernfs/inode changes relative to Lab v1;
only kernel/dma/Kconfig and coherent.c differed. In particular no
userspace SRAM mapping or callback execution preceded the first
stall. The optional temporary CPU-only arena proof and misc
registration do run during boot; their effects are not excluded.

## Historical evidence

Scanned the private evidence archives' text/log/raw records and
then an additional 188 logs/records, including .uart members.
One earlier RCU report found in 02-B.uart of original Lab v1
USB authorization A/B (2026-11-26):
~/deps/s31-research/bench-evidence/usb-auth-ab-20261126T232121Z/evidence.tar.gz
SHA-256 026cc0bbf81300e97d13fd479bb93652e435910a198a4e8e20694f5c734a79ed.
The earlier Oops occurred at 2.217099 s in usb_hub_wq,
__slab_alloc_node.isra.0+0xc6, bad address 00687333, trace via
kernfs/device registration. At 23.417990 s RCU reported CPU1
one GP behind. See BOOT-DEBUG-LIST-MATCHED-20261126.md on the
existing FPU qualification branch for exact original-symbol analysis.
That earlier signature is different from the current CPU0 worker
stall; it demonstrates pre-existing instability, not identity of
root causes. Most historical observations were short, so absence
of a logged RCU report is not evidence that a boot would never stall.

## Discriminating control

The current candidate does not include 1c2d2160bff76b11b81c2dc4bd5ef1ecb7275094,
the prior early noncoherent DMA alignment fix. setup_arch calls
riscv_set_dma_cache_alignment before the S31 cache driver's arch_initcall,
so dma_cache_alignment falls to 1; ARCH_DMA_MINALIGN is 64.
The configured SWIOTLB + DMA_BOUNCE_UNALIGNED_KMALLOC path uses
dma_get_cache_alignment for the size-alignment decision. The USB HCD
maps input transfer buffers DMA_FROM_DEVICE. Prior instrumented
comparison showed eight 1..32-byte USB input mappings change from
direct mapping to SWIOTLB bounce with the correction. Invalidation
of partial cache lines sharing slab objects is a plausible memory
corruption mechanism, not a diagnosis of the current stall.
The original authorization experiment cannot exclude enumeration
or DMA because it did not disable the USB controller.

Created isolated kernel control branch
experiment/s31-wifi-sram-dma-alignment-control,
commit 68bee8b8f52f05ecc472effa3bb90201d056c4ef.
Cherry-picked only the previously developed 13-line alignment fix,
no F extension changes, no RCU or kernfs workaround, identical
.config verified after olddefconfig. Full build launched PID116106,
separate output ~/deps/s31-research/wifi-first-20261127/alignment-control-build.
Check alignment-control-full.status, alignment-control-full.log
and ALIGNMENT-CONTROL-SHA256SUMS before any physical use.
No claim of a fixed stall; retain original baseline/rollback.

Astra read-only review was attempted but reached its 55 s timeout;
no completed independent conclusion. Private review log retained.
No change to RCU timeout, preemption, debugfs or scheduling settings
to hide a stall. Next matched tests should compare the unchanged
session candidate and the alignment-only variant, same DTB and
peripheral configuration. Repeat sufficient times; if needed
separately remove the physical USB device, not merely defer
authorization. Retain all faults and do not infer causation from a
small negative sample.

## Idle physical control

No allocator ELF, device open, debugfs mount or data traffic.
Observe the raw UART for 100 seconds after shell profile and boot ID,
then read dmesg and recheck boot ID. Same board, connected USB device,
Ethernet cable, original DTB and rootfs. Hardware reset assisted by
esptool; console itself may reset. Baseline Lab v1 passed the 100 s
idle window, same boot ID, both harts, eth0 present, USB disk attached.
No RCU stall or screened kernel faults observed. Single observation
only. Candidate boot and idle control initiated after verification of
all five original flash slots and full 4300800-byte rollback span.
Private evidence:
~/deps/s31-research/wifi-first-20261127/rcu-evidence/
rcu-baseline-evidence.tar.gz SHA-256
1567760ce52db52e8dc062b509e797a87cf878e2723a6ec3830182011af4db53.

Candidate idle run: same 100 s observation completed, no RCU stall
or screened Oops, no reboot. Both harts reported online, eth0 present,
lease device exists but was not opened. USB descriptor read/64 timeout
-110 at 7.377671 s; mass storage detected at 17.915528 s and SCSI disk
attached at 19.227267 s. The separate log screen flagged the USB
timeout. Do not equate no RCU observation with platform PASS.
No reproduction of the original stall in these two single idle runs.
The negative observations do not distinguish the effect of the prior
serial upload, debugfs mounting, boot timing or other incidental work;
nor do they exclude a kernel bug. Both tests used uncorrected DMA
alignment. No allocator callback executed. Candidate private evidence:
rcu-candidate-evidence.tar.gz SHA-256
e1e942daf3f8784036fad557babc942d175aea4c96f624efe75409567eb43eff.

Added screen-kernel-log.py and seven host unit tests. The classifier
finds the two previous candidate RCU reports and the historical Oops/
RCU record, detects current candidate USB timeout, and returns no
matches on idle baseline dmesg. It does not declare overall PASS or
replace profile, timing and transport checks. Future qualification
must integrate this screening, retain logs and fail on RCU reports.

Final rollback verified exact original erase span, all five Lab v1
slots, original runtime boot, eth0 present, both harts and UART free.
Session exit 0 for the bounded idle observation, not a radio or USB
qualification. All three new evidence archives independently verified.
rcu-restored-evidence.tar.gz SHA-256
7c3d73363c7fda7a0acedeb9a311a7f594dc9469b705e03d45a6023a99311858.

## UART/tmpfs workload control

An additional original Lab v1 boot ran the exploratory v1 runner:
80 KiB deterministic synthetic data, 214 paced base64 UART chunks,
target SHA-256 be73f91b0f28f2a6c43b00e4dc8afb72f14d6040f88b27b76df6740abcc96c1c,
read-only debugfs mount/unmount, temporary files removed, then 100 s
passive observation and dmesg/boot ID/profile recheck.
No ELF execution, radio, lease access, USB media writes or flash
writes. All four control markers, OBSERVED, zero screened alerts,
exit 0, UART free. This is a single exploratory negative result;
it does not establish that UART activity is harmless in all boots.
Actual mount listing showed one /tmp tmpfs rw entry; v1 did not
enforce exclusive mounts or exact USB identity. Preserve the v1
runner, not treat it as equivalent to the hardened test.

Private baseline-uart-evidence.tar.gz (110499 bytes) includes exact
v1 runner, screen, full commands/raw console and before/after dmesg,
result JSON, reset log. SHA-256
6203803f2b6759c7d5ff879ab4d1a7dc8e4a7268019302574112d9314add2251.
Transferred in 12 chunks, reconstructed digest verified on Linvora-dev.
All four RCU evidence archives verified against SHA256SUMS.

Hardened v2 runner rcu-uart-control.py checks exact profile and lease
node/absence, mount exclusivity, direct USB topology, explicit expected
VID:PID and known model/geometry, before/after identity and periodic
boot/USB/dmesg checkpoints. Stop on any screened alert, retain evidence
and mark cleanup pending before possibly lost acknowledgements.
No blind cleanup on failed console. Reset by verified caller if needed.
USB snapshots cannot establish continuous attachment. Trusted lab only,
not secure against concurrent privileged mount changes. Two Astra
read-only reviews completed; remaining findings addressed (expected
VID:PID, direct topology, regular file/dangling symlink at lease path).
Final changes not independently re-reviewed. Eight log screening and
four control host unit tests passed. Hardened v2 copied and hash
verified on Orion; not yet physically exercised. Capture the actual
expected USB VID:PID using read-only inventory before running. Run
identical hardened v2 controls on baseline, uncorrected session and
alignment-only candidate. Keep physical identity and configuration
constant; each candidate flash session requires exact span backup and
verified rollback. Alignment build still pending in this checkpoint.

## Alignment-only candidate: first complete physical control

Full build status 0, all five artifacts verified. Configuration
and DTB are byte-identical to the uncorrected session candidate.
Only the 13-line DMA alignment patch differs in source.
Kernel XIP SHA-256
e45f05329785b053503be996f9e08c37cbc59c5fab78e6d19fa93b7bffb6ba78,
size 4298508 bytes; vmlinux SHA-256
b4c5b0ed74829b283d16c879255f5df629ad15f4019b1042fa4ad77e321964d8.
Private draft transport with scoped asset URL, no personal
credential restoration on Orion.

Before flashing, an original Lab v1 inventory boot failed with
CPU1/PID38 esp-example-tar, EPC c00af0e0, RA c00af098, s2=0x11a,
badaddr 0x11e, cause 0xd, instruction halfwords 2a03 0049.
This exactly matches the historical anon_vma_clone+0xb8 signature
documented in BOOT-FAULT-SCREEN-20261126.md. This is not a new
candidate failure and does not prove a shared mechanism with RCU.
All five original flash slots and the full erase span subsequently
verified. Another boot succeeded and read-only inventory pinned
USB 0951:1666, direct port 1-1, Kingston DataTraveler 3.0,
121077761 sectors.

Hardened v2 aligned-01: CONTROL_PROFILE_PASS, confirmed 64-byte
alignment log, USB identity, all 214 UART chunks, payload digest,
cleanup, 100 s observation, distinct before/after profile checks
and unchanged boot ID within the run. CONTROL_RESULT OBSERVED 0,
no screened alerts; exit 0. No radio or SRAM lease operation.
Exact rollback span, five slot digests, original runtime and UART
free all confirmed. This is one negative observation, not proof
of a repaired RCU stall.

Private evidence (all SHA-256 verified on Linvora-dev):
inventory-oops-evidence.tar.gz
fdff9934fb4f31da09aff0fbede94549ac210a1fb60ec5604080507f91986f0a
inventory-retry-evidence.tar.gz
a389d7d7d18f52c8b66c6597f1a1d8de8e3d5fd5a46924870ce1f8f89997085a
aligned-01-evidence.tar.gz
2786df52ad461ccc5d58594dc14ad00d4d7ab01b731eb9855cf28e52d05b35f6

## Bounded three-boot follow-up (user-approved)

User requested three additional cycles as an initial gate, with a
larger reliability campaign deferred until Wi-Fi works. A single
verified aligned kernel flash followed by three ROM-assisted RTS
resets and 100 s observation per boot. Same USB identity, both harts,
eth0 presence, read-only rootfs and alignment message; screen logs
and require three distinct boot IDs and no alerts for 3/3 PASS.
No UART payload upload, radio, SRAM lease, Ethernet traffic or USB
media writes in these boot-only repeats. Not physical power cycles.
Restore full original erase span and verify all slots afterward.
Exact scripts copied and digest-verified in private rcu-evidence/
repeat-03-procedure/. Boot runner b5748eafef0bf3d515a18d25f1218c44e77b36dd3f247022e52cb2bd14b27165;
session wrapper 019cd313e1c4f35c0dc82a10e0944cedf35a1e7ca8deeead3830214c159efd76.
Session started on Orion, PID619929, output
~/source/build/s31-rcu-investigation-20261127/uart-control/hardened-v2/repeat-03/session.log.
No three-cycle result claimed at this checkpoint.

## Three-cycle initial gate complete

All three additional aligned boot-only cycles completed, each with
100 seconds of observation and no screened alerts. Distinct boot
IDs verified. Both harts, eth0 existence, exact USB identity,
read-only CramFS, 64-byte alignment log and unchanged boot ID within
each run confirmed. No radio, SRAM callback or peripheral traffic
qualification. Exact full span restore, five original slot digests,
restored runtime and UART free all PASS. The runner exited successfully.
Together with the previous complete UART/tmpfs test this gives one
workload observation and three boot-only observations on this exact
candidate. It does not prove a causal DMA fix or population reliability.
Private repeat-03-evidence.tar.gz SHA-256
61c4c28f376e24b5b361e9972c98474a16cd50058e6529c4ab67cc267a5a4883.
All eight RCU evidence archives verified on Linvora-dev.
User approved adopting the aligned base for Wi-Fi work and deferring
the larger (20-cycle) reliability campaign until Wi-Fi is operational.

Kernel integration/s31-native-wifi now points at the exact tested
68bee8b8f52f05ecc472effa3bb90201d056c4ef, published to the personal
fork. No upstream merge or production-release claim. The separate
experiment/s31-native-wifi-f-only adds the two previously tested
F-only patches (original a1b315e2e4ba, 060679ff35be) to this base.
New HEAD 95d9d308df6e980d4b1a421c694be09b290c79a9, pushed.
Combined qualification is still required; see handoff.
