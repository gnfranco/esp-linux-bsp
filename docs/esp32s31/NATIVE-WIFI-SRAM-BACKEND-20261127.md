# Native Wi-Fi OSI allocation backed by the SRAM lease

Parent BSP 304ed52f1bae26c2f7069b2e652a80c9c9ba14dc.
Use the previously physically qualified kernel
e328a231fcc71494777b014d77302b230e3c21ca (xipImage SHA-256
180a6041f4d7a785c99a8dc9e73b383b233cf22eceebb63b609dc7bfd5be4401).
No kernel change or new full kernel build.

## CPU-only backend

radio-sram-backend.{c,h} implements the existing radio_backend
contract against a caller-owned exclusive 16 KiB mapping of
/dev/s31-wifi-sram-lab. Metadata lives in normal process memory.
Internal classes share a 256-entry bitmap of 64-byte units.
First-fit aligned contiguous runs, exact rounded charge, no fallback,
reject requests exceeding mapping capacity or alignment 4096.
Scrub each internal allocation before returning its units to the
same lease. General allocations use aligned_alloc/free outside this
mapping (charged payload extent, not total malloc allocator overhead).
The Wi-Fi OSI adapter's existing pthread mutex serializes all
allocations/releases and accounting; callers drain borrowers
before free/realloc/close. No callback reentry, ISR, vendor worker,
or registration. No bus address or actual DMA despite the legacy
logical RADIO_INTERNAL_DMA label. The arena is not a measured
production capacity. INTERNAL class limit zero in this Wi-Fi test.
Backend lifetime is one caller-owned mapping, not independently
reentrant, never reinitialize live backend; valid release arguments
come only from the ledger adapter. Any impossible release state aborts
the experimental process. This does not constitute a complete
allocator hardened against arbitrary corrupted metadata.

## Tests

Host anonymous mapping is explicitly a substitute for SRAM; device
mode opens and maps the real kernel lease, compares payload with
pread from kernel, checks mapped extent and 64-byte alignment.
Tests general/internal separation, calloc, realloc failure with old
content preserved, successful resize and scrub of the old extent
through the owning mapping, exhaustion, fragmentation, full-arena
recovery, 4 workers x256 internal calloc/free cycles, drained
backend, zeroed mapping, close/unmap and kernel quarantine until
reboot. The ledger limit intentionally allows 64 bytes above the
real capacity in this test so the full-pool denial exercises
the backend itself. It is not a production resource policy.
Astra high read-only review found expired pointer use in the scrub
test and masked backend capacity check; corrected both before final
build. No second independent review. Host normal and ASan/UBSan
with leak detection pass; 3 test groups each. RV32IMAC static ELF
compiled with GCC14.1.1, no vendor archives linked.

Reproducible build:
```sh
cd experiments/s31-native-radio/resources
./build-radio-sram-backend.sh "$HOME/deps/s31-research/radio-demand-20261127" \
  "$HOME/deps/s31-research/wifi-first-20261127/osi-sram-build-new"
```
Final build osi-sram-build-03, all five hashes verified.
RV32 ELF SHA-256
a07ebbabfb2c838a35f27768ac32f4576af46ed9d84106b917a671a3baac1e58
gzip SHA-256
c0ad6af763be8e0adc18bd0d81b622fe7167e882172fe0460fa9632d155bfbcc.
No binary added to public source repo.

## ROM address classification (static follow-up)

The previously reproduced 297 ROM-map name matches separate into
194 addresses in the ROM aperture and 103 addresses in IDF's reserved
ROM SRAM range 0x2f07afb0..0x2f080000 (outside the global DMA pool).
Seven names have different addresses in libgcc.ld and rvfp.ld:
__adddf3, __fixdfsi, __floatsidf, __floatundidf, __floatunsidf,
__muldf3, __subdf3. Selection depends on runtime/ABI, not arbitrary
linker map order. This is address classification only: the aperture
contains code and data, and SRAM contents may depend on firmware
initialization. No access permission or runtime compatibility inferred.
No ROM addresses mapped or called. Reproduce with:

```sh
d="$HOME/deps/s31-research/wifi-first-20261127"
python3 experiments/s31-native-radio/wifi-rom-addresses.py \
    "$d/rom-maps" "$d/repro-01/wifi-rom-matches.tsv"
```

S31_WIFI_ROM_ADDRESS_AUDIT_PASS; private result
rom-address-audit.json.log SHA-256
03485538a4bc4210c3fa36b38ab715f5524f78955ab55968b3f9be4cfc44185d.

## Physical test: functional PASS, overall session INCOMPLETE

Original five Lab v1 slots and full 4300800-byte rollback span
verified immediately before writing the previously qualified kernel.
Candidate digest matched independently. Exact test ELF digest
confirmed on target tmpfs. All three groups passed:
SRAM_OSI_CLASSES_REALLOC_PASS,
SRAM_OSI_EXHAUSTION_FRAGMENTATION_PASS,
SRAM_OSI_THREADS_CLEANUP_PASS, and S31_SRAM_OSI_DEVICE_PASS, rc=0.
Pool before: 2 used/62 free pages; after: 6 used/58 free pages,
tracked counts matched, peak 6 pages, zero pool allocation failures.
Exactly four pages quarantined as intended; boot ID unchanged,
both harts online, eth0 present, target executable removed.

However, at 23.407735 seconds after boot (during transfer, before
the allocator executable was run), rcu_sched reported a CPU0 stall
in kworker/0:2, workqueue events. EPC 0xc010d2ae resolves against the
exact candidate vmlinux to kernfs_notify_workfn+0xb4, in the loop
traversing kernfs superblocks. The second stall was logged at
86.419623 seconds during debugfs unmount. The serial command timed
out, so final unmount and candidate reboot test were not confirmed.
The harness's original fault filter did not classify RCU stalls;
tighten it before the next test. No causal link to the allocator
established; no proof of root cause. Do not waive this anomaly.
No SRAM_OSI_HARDWARE_PASS or SRAM_OSI_REBOOT_PASS overall markers.
Session candidate_rc=1. Functional callback results are retained,
but this is not full platform qualification. Next isolate kernfs/RCU
with a candidate boot and no test workload, compare restored baseline
under the same peripheral and UART conditions. No production driver
or vendor initialization until the reliability gate is addressed.

Final rollback: exact original erase span verified, all five
original slot digests verified, Lab v1 boot and both harts confirmed,
eth0 present and candidate marker absent, UART free.
Evidence in private Linvora-dev cache:
~/deps/s31-research/wifi-first-20261127/physical-evidence/
osi-sram-candidate-evidence.tar.gz SHA-256
f1ad2293a7028bea93fbebeb399cdca0c099f215af6c3cbf351ba6c4134287f7
osi-sram-restored-evidence.tar.gz SHA-256
5c7f6ff15a1a257b3b8d440a0b18b91d6337f9bb426eb22f34cb5d339d15ab36
Both hashes and all previous evidence archive hashes verified.
