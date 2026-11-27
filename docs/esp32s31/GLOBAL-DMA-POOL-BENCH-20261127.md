# Global DMA pool qualification: comparative bench harness

Date: 2026-11-27. Linux candidate:
7a542131c806aaae330579a9955ff728506e15b1.
See GLOBAL-DMA-POOL-STATS-20261127.md for instrumentation and reproduction.

## Full build

Started an isolated out-of-tree build in
~/deps/s31-research/global-dma-pool-stats-20261127/build on Linvora-dev.
The existing source checkout is clean on the candidate commit.
The prepared config differs from original Lab v1 only by
DMA_GLOBAL_POOL_STATS=y. The command is make O=... -j2 xipImage dtbs
with the previously qualified RV32 cross compiler and host tools.
The build log, exit status, and (on success) linked image hashes are
written in the evidence directory. A running build is not a successful
build; do not flash before checking exit status, output hashes, slot size
and the exact rollback artifacts.

## Test harness

experiments/s31-native-radio/resources/pool-stats-qa.py is a Python
UART harness for the original CramFS Lab v1 and instrumented profile.
It takes the existing UART helper and private USB reference JSON as
inputs, checks the expected Kingston medium and high-speed enumeration,
and allows up to 60 seconds for asynchronous peripheral readiness.
It does not configure the local network or write to the USB medium.

Checks:
- Same boot identity, dual-core RV32, read-only CramFS, no /dev/sd*
  mount, existing RAM workspace, local eth0 default gateway.
- Eight 128 KiB USB hash samples against the previously validated medium.
- Ten ICMP packets to the configured local Ethernet gateway.
- 32 MiB bounded USB read and a second disjoint 32 MiB read with Ethernet.
  Job lifetimes and joint progress in a counter sampling interval are
  required, but do not prove cycle-level simultaneity or exclusive traffic.
- Per-phase block sectors and Ethernet RX/TX counters, elapsed host time
  (includes serial control overhead; not a precise throughput benchmark).
- Candidate only: read-only debugfs snapshots, bitmap/accounting consistency,
  nondecreasing cumulative counters and zero new allocation failures.
- Kernel error screen, both harts still online and cleanup of RAM artifacts
  and any debugfs mount created by the harness. PASS is recorded only
  after cleanup succeeds.

The parser passed two valid and ten invalid fixtures on the host;
the tests also passed with python3 -O before the readiness-only change.
Astra high read-only review identified error-screen masking, cleanup
false PASS, insufficient concurrent progress evidence and allocation
failure criteria. The changes above address those findings. The updated
harness still requires physical qualification.

Example (Orion only, after validating original rollback hashes and
confirming the bench medium and local network are authorized):

```sh
python3 -u pool-stats-qa.py \
  --helper /path/to/qualified/run-uart-v4.py \
  --reference /path/to/private/result.json \
  --profile baseline --out baseline-new
```

For the candidate use --profile instrumented and a fresh output directory.
Keep logs private and restore Lab v1 after candidate testing.
The harness is not a flash tool. No candidate flash is authorized
by its output alone.

## Observations

First baseline attempt: console unavailable before the workload.
Second attempt: Linux console available but expected block device
not yet confirmed. Read-only preflight subsequently saw a high-speed
USB event and the Kingston SCSI identification after the initial
SDA_ABSENT observation; no local eth0 default route at that sample.
The readiness wait was added to avoid attributing an asynchronous boot
condition to the instrumented kernel. Neither attempted workload ran.
No candidate flash was written.

Third attempt with readiness wait: console unavailable before readiness
checks. Result INCOMPLETE, no workload samples, UART released. The three
attempts and USB preflight logs have been preserved on Linvora-dev under
global-dma-pool-stats-20261127/bench-evidence with verified transfer hashes.
At 13:03 UTC the full build was still active (402 log lines), with 5.1 GiB
free. The original flash image remains unchanged; no DMA capacity claim
or instrumented runtime result has been established.

## Full build and physical USB qualification

Full kernel build completed with status 0. Kernel commit unchanged
7a542131c806aaae330579a9955ff728506e15b1; config difference still only
DMA_GLOBAL_POOL_STATS=y. xipImage 4294412 bytes, SHA-256
13f0d4f93b52e7fd9e0a1dd737c951d6472ff320dcfee36eb757d0e4a6de3024.
The four full-build checksums verified and System.map contains
dma_global_pool_stats_show/init/fops. Transferred the kernel only through
a draft in the private esp-linux-lab repository, tag label
transport-s31-global-dma-stats-20261127 (not a release); both computers
verified SHA-256.

Fourth baseline attempt confirmed USB readiness, but no eth0 default
gateway appeared within the readiness window. Added explicit --usb-only
scope, which never reports a full qualification PASS; candidate stats
are also sampled at console availability before peripheral readiness.
Parser tests pass. No network reconfiguration attempted.

Physical results on original Lab v1 DTB:
- Baseline USB-only PASS: eight reference hashes (1 MiB), 32 MiB bulk
  read, 66240 additional block sectors, host elapsed 7.269 s.
- Candidate USB-only PASS: same hashes, same bulk bytes and sector
  count, host elapsed 7.909 s. Ten coherent pool snapshots all agreed:
  4096 bytes/page, 64 pages total, 3 used/tracked, 61 free, peak 3,
  largest allocatable 32 pages, 0 allocation failures and 0 delta.
  Thus 12 KiB used, 244 KiB free, 128 KiB largest aligned extent.
  Both cores remained online and no tested kernel error was observed.
- No Ethernet workload or concurrent workload: gateway absent; Ethernet
  counters unchanged. USB-only timings include serial/control overhead,
  not a precise throughput regression measurement. No radio allocation
  or resource reservation was made.
- Before flash all five slots verified; candidate kernel slot verified;
  original kernel restored and all five slots verified again. Restored
  baseline USB-only PASS (7.212 s); UART free. No DTB/rootfs/USB write.

Logs and JSON copied from Orion to Linvora-dev, transfer hashes checked,
under global-dma-pool-stats-20261127/bench-evidence. Candidate and restored
tests both completed, not an in-progress flash procedure.

## OpenSBI footprint: source-derived reconstruction

Recovered exact fw_dynamic.bin and associated ELF on Orion in
s31-reboot-validation/opensbi-rom/platform/generic/firmware. Reconstructed
the file-backed PT_LOAD image from the ELF and matched the FIT payload
byte for byte. Payload SHA-256:
0e179f805a5272b5d3d3bbdb1fb81a79a564a5fc4b3dcb4a4f17715c8b5009fa.
ELF SHA-256:
467d8e22b81a71656a3f4a4617f768406a8eadb6e2f4cc6fd35739bc22820ba8.
Symbols: _fw_start=0x2f000000, _fw_rw_start=0x2f020000,
_bss_end=0x2f023b90, _fw_end=0x2f024000. The FIT fdt-1 extracted at
data-offset 0x3e62c contains two enabled CPUs and opensbi,config
heap-size=0x7000. Pinned OpenSBI commit 0940d8a5834d02b93e2be0e60ef6505767862875
platform/generic/platform.c uses that heap override; fw_base.S adds
two 8192-byte stacks (including scratch) then the heap after _fw_end.
Derived firmware end is 0x2f02f000 (188 KiB from base). The domain
region builder rounds the RW interval 0xf000 to a naturally aligned
0x10000 region ending at 0x2f030000, which is exactly the pool start.
This is source/artifact-derived, not live scratch/PMP register measurement.
Do not consider the geometric 4 KiB gap allocatable. Further runtime
ownership confirmation remains required; no modifications to OpenSBI.

## Ethernet readiness resolved on Lab v1

The original rootfs /etc/network/interfaces contains only auto lo /
iface lo inet loopback. S40network runs ifup -a, so eth0 was initially
administratively DOWN, with no DHCP process. This explains the missing
gateway on previous attempts, not evidence of a bad cable or PHY.
Using the authorized local bench, ip link set dev eth0 up yielded
carrier=1, speed=1000, duplex=full. udhcpc -i eth0 -n -q -t 5 -T 3
obtained a lease; 5/5 ICMP echoes to the local gateway succeeded.
Private evidence ethernet-preflight-01.log and ethernet-activate-01.log
transferred and hash-verified on Linvora-dev.

Harness now accepts an explicit --configure-ethernet option in full
scope. It raises the interface and requests a lease only if an eth0
default route is absent. Changes are runtime-only; no rootfs modification.
Candidate records pool snapshots before activation, at carrier and
after DHCP. USB-only scope is unaffected. The parser tests pass.
This establishes local Ethernet connectivity on the uninstrumented
Lab v1; full combined workload and instrumented Ethernet qualification
are separate tests.

Baseline full-scope qualification: PASS after explicit runtime Ethernet
activation. Ethernet 10/10 ICMP replies, USB 32 MiB read, concurrent
32 MiB read plus 10/10 ICMP replies. Both job lifetimes overlapped and
both device counters advanced in a common sampling interval.
Eight USB reference hashes matched. No cleanup errors.
Astra high follow-up review noted that the opt-in path is specific to
the dedicated bench: it could race another DHCP client in a different
network setup, and runtime network settings are retained until reboot.
Do not use the option on a multihomed or otherwise managed system.
The original observed Lab v1 had only loopback autoconfiguration and
no DHCP client, and no persistent image change was made.

Instrumented full-scope attempt: INCOMPLETE. All five baseline slots
verified and candidate kernel verified. The Ethernet carrier and DHCP
were established, USB identity and all eight reference digests passed.
Ten pool snapshots: 3 pages at boot-console, 5 pages after link-up
and after DHCP, at idle and across six Ethernet samples. Thus 20 KiB
used, 236 KiB free, peak 20 KiB, largest extent 128 KiB, zero pool
allocation failures. This does not measure concurrent USB/Ethernet
bulk traffic, and is not radio admission capacity.
The first Ethernet workload failed the required 10/10 ping summary
criterion; the exact reply count was not retained by the v6 harness.
Do not infer causal regression from one failed summary. Added raw ping
output, exit code and parsed counts to private result JSON before
assertions, preserving the strict pass criterion; parser tests pass,
new diagnostics not yet qualified physically.
Candidate procedure restored the original kernel and verified all
five flash slots; no DTB/rootfs/USB write. Private logs are preserved.

Final rollback: five flash slots verified; the original Linux booted
and reached the post-restore workload, but its repeat full test stopped
on "I/O counters unavailable". This is an incomplete repeat, not a
full functional PASS. UART released. Earlier baseline full PASS remains
valid for that run. Preserve raw evidence, improve diagnostic capture
and repeat both profiles before attributing the ping criterion failure
to instrumentation. No further flash performed in this session.

## Repeat with labelled UART diagnostics

The v7 harness stores every completed command response and return code
in a private uart-commands.log, including responses rejected by the
parsers. Transport exceptions are logged, but a timed-out partial byte
stream is not recovered by the existing helper. I/O counters are read
into shell variables and emitted on a single POOL_IO line, then checked
for uniqueness and numeric fields. Ping summary parsing tolerates
leading horizontal whitespace but rejects missing/duplicate summaries;
10/10 remains mandatory. Runtime-only Ethernet help now explicitly
states that link/address settings persist until reboot.
Eight unittest methods (including subtests for bad and ambiguous inputs)
passed on host, normal and optimized Python.

Repeated Lab v1 full test with v7: PASS, 10/10 Ethernet and 10/10
concurrent ICMP replies, two 32 MiB reads, reference hashes matched,
joint counter progress and overlapping jobs, no cleanup errors.
Evidence archive baseline-full-02-evidence.tar.gz copied and verified
on Linvora-dev. The same candidate kernel and same workload are used
for the repeat; no new kernel build or allocation change.

Astra high read-only review of Linux commit 7a542131: no change in
allocation decisions or per-packet hook; small accounting operations
under the pool lock and snapshot copying with IRQs disabled may affect
timing, and the snapshot allocates ordinary kernel memory. These are
hypotheses, not evidence that the earlier failed ping criterion was
caused by instrumentation. If failure persists, compare baseline,
candidate without debugfs reads, and candidate with sparse timestamped
reads, using identical traffic and multiple repetitions.

V7 instrumented repeat: isolated Ethernet PASS (10/10), isolated USB
PASS (32 MiB), all eight reference hashes matched. During concurrent
workload the console command reading I/O counters timed out after five
successful concurrent pool snapshots. Total 23 snapshots: occupancy 3
or 5 pages, peak 5 pages, largest extent 32 pages, zero allocation
failures. Concurrent completion and ping summary are unknown. Target
cleanup command also did not return promptly; only the QA Python
process was interrupted, allowing parent rollback to proceed.
All five original flash slots verified after restoring the kernel.
Post-restore original Lab v1 booted, Ethernet 10/10 passed; during
isolated USB read the same counter command timed out. Its cleanup
wait was also interrupted; UART released, no additional flash writes.
Both JSON results are INCOMPLETE (empty cleanup_errors in v7 is not
evidence of completed cleanup after KeyboardInterrupt). Similar
symptom without instrumentation makes a unique instrumentation
cause unsupported, but does not establish whether the console,
counter access or target execution stalled. No further test run
in this session. Private evidence archives baseline-full-02-evidence,
full-session-v7-instrumented-evidence and full-session-v7-restored-evidence
all transferred and hash verified.

Prepared next diagnostic harness: explicit --stats-unread control for
instrumented full profile (no debugfs mount or reads, distinct
UNREAD_CONTROL_PASS marker). Timestamped command capture retains
partial output on timeout, with separate markers before disk, RX and
TX counter reads. KeyboardInterrupt is recorded as incomplete cleanup.
Ten host unittest methods pass in normal and optimized Python, including
mock serial completion and timeout partial capture. New diagnostics
not yet physically tested. The Linux stmmac_get_stats64 path reads
queue statistics using u64_stats_fetch_begin/retry; this is one
possible location to distinguish from serial loss or block-stat access,
not a diagnosed deadlock. Compare repeated baseline, unread control,
and sampled candidate before claiming concurrent qualification or
assigning a radio allocation budget. Keep original image as reference.

## V8 baseline and independent driver inspection

First v8 baseline attempt stopped at the shell readiness gate; no
workload. One flash-id recovery reset (no flash write) restored the
console. Second attempt: full PASS, 20/20 total ICMP replies, two 32 MiB
USB reads, all eight reference digests, joint counter progress and
overlapping jobs, both harts online, cleanup completed. The archive
baseline-v8-evidence.tar.gz is private on Linvora-dev, SHA-256
29361c586144a422b394c0f873f68fbaf915fbd02128478bb4b9d3f3e40e62a9.
V8 code digest 80ba9666c3cb133a338809ce2eff747a610b2f6141f4fbe99332f4cd82cf74e8.

Astra high read-only inspection of stmmac_main.c and u64_stats_sync.h:
all inspected queue seqcounts initialized, matching update begin/end
pairs, and preemption disabling in RV32 u64_stats updates. No concrete
unmatched writer or preemption defect found in this limited inspection.
Writer mutual exclusion across CPUs and IRQ-context correctness
remain broader questions, not established by matching pairs.
If a future counter read stalls, use partial stage markers and an
independent liveness check; stack/PC and seqcount evidence would be
needed to attribute a deadlock. No driver change.

Control protocol: exact previously verified candidate kernel, first
--stats-unread, then a separate boot via flash-id and sampled profile
if the control passes. Verify original flash before write, verify
candidate, restore and verify all five original slots on exit.
No modification to the pool capacity or radio allocation policy.

V8 unread candidate control: exact candidate flash verified, fresh
boot, Ethernet isolated 10/10, USB isolated 32 MiB, all eight
reference digests. Both jobs progressed in the concurrent phase;
the second 32 MiB USB read finished (512+0 records in/out, rc=0).
Actual ping transcript: 10 transmitted, 9 received, missing sequence 5,
10% loss, command rc=0. Thus the strict full workload criterion failed,
not a parser failure. Zero debugfs pool reads and zero pool snapshots
in the entire control. The sampled phase was not started because
the control failed. Cleanup returned successfully. This establishes
that a debugfs stats read is not necessary for the observed loss; one
short run does not attribute loss to instrumentation or the driver.
Private archive full-session-v8-unread-evidence.tar.gz, SHA-256
31eba89f81785857d643c520603780ad83b8511efce661a8257cc2f0759c77aa.
All five original Lab v1 slots verified on rollback.

Next distinguish rare traffic loss from console failure with repeated,
longer paired runs and a separately recorded local endpoint observer
on the authorized bench. Use fewer UART commands during the actual
workload, keep packet sequence/RTT and read completion independently,
and record boot identity and pre/post counters. An independent host
probe should be outside the quantitative workload or explicitly included
identically in both profiles. Do not equate ICMP nonresponse at a router
with Ethernet frame loss. If reproducing a target stall, distinguish
kernel liveness from UART access and inspect the previously qualified
early DMA alignment fix as a separate comparison (not a proven cause).
No new radio allocation or driver patch justified by this result.

V8 post-restore baseline: original flash verified, Linux booted,
isolated Ethernet 10/10 and USB 32 MiB passed. Concurrent workload
again lost the UART command response. Partial capture contained a
corrupted/truncated command echo ("princlass/net/_IO_DISK_BEGIN...")
and no executed counter-stage marker. This does not locate a stall
in stmmac_get_stats64. While UART cleanup was still unresponsive,
an independent ping from Orion to the board's DHCP address, matching
the previously observed board Ethernet MAC in its neighbor table,
returned 5/5 replies (0% loss). This proves the target IP stack
responded during that console outage, not that every kernel task
remained healthy. This probe was after the workload timeout and
is not counted in the comparative traffic results.
Interrupted only the stuck post-restore QA cleanup; UART released.
Flash remains original Lab v1. Post-restore full result INCOMPLETE.
Private full-session-v8-restored-evidence.tar.gz transferred and
hash verified, including partial UART output and independent ping.

Prepared next transport experiment: optional --paced-uart sends
32-byte bursts with 10 ms gaps and rejects short writes; counter
expression is installed as a shell function at idle, then invoked
by a short pool_io command during load. Twelve unittest methods
passed in normal and optimized Python, including sender byte
preservation and short write detection. No claim that pacing fixes
the underlying UART issue; physical validation pending. Do not
change the kernel DMA statistics or stmmac driver on this evidence.

## V9 paced UART baseline

With 32-byte bursts, 10 ms gaps and preinstalled short pool_io shell
function, two complete Lab v1 runs passed: total 40/40 ICMP replies,
128 MiB bulk USB reads, both sets of reference digests, joint activity,
both harts online and successful cleanup. No UART timeout in these two
runs. First run preceded by flash-id recovery reset; second without a
new flash write. Private baseline-v9-evidence.tar.gz transferred and
hash verified. This is promising transport mitigation, not proof of
the root cause or long-run stability. See UART-S31-REGISTER-AUDIT-20261127.md
for concrete S31-vs-S3 register differences and an independent driver
porting plan. No kernel changes for this audit.
