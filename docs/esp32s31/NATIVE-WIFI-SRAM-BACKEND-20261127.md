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
