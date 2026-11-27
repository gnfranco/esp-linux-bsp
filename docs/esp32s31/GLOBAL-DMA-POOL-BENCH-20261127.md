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
