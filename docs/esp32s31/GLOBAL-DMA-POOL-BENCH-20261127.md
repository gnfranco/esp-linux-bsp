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
