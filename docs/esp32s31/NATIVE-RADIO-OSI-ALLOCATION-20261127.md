# Native radio OSI allocation binding

Date: 2026-11-27. Parent d127c7327edb8655c7fc660147177dd87c54d7f1.
The typed allocation adapter now supplies the allocation callbacks of
the official Wi-Fi and coexistence structures and the two BTDM
allocation symbols for the S31 multi-link profile. This is a partial
ABI binding tested with a simulated backend, not a radio startup.

## Pinned interface

ESP-IDF v6.1 commit fff9895c82d744c7237be8847347bdd1b07c6643:
- components/esp_wifi/include/esp_private/wifi_os_adapter.h,
  blob 0495a83c5c2a28516e4997a69b9f596de9a86a0c (OSI v9).
- components/esp_coex/include/private/esp_coexist_adapter.h,
  blob 15e8ef7114fefeba8671c51a42f0e60ee9de0ec8 (coex v2).
- components/bt/porting_btdm/controller/btdm_common/include/btdm_osal.h,
  blob 1975300b49b1a921eb29e6b2b2d9b79740cdb319.
The build script verifies all three hashes and creates a minimal test
sdkconfig for S31, HE and BT multi-link. This is not a full IDF build.
Official headers remain in the private dependency cache, not vendored.

Ten Wi-Fi allocation slots and two coexistence slots are assigned
without function-pointer casts. Other fields, including version,
magic, timer functions and free-heap query, remain unchanged.
The BTDM bridge exports wr_btdm_osal_malloc/free with the original
declarations, and refuses non-multilink builds at compile time.
No vendor archive was linked or controller invoked.

## Policy and limitations

A single process lifetime owns a static allocator with a mutex.
Task context only; backend reentry, signals, cancellation and ISR
calls are unsupported. The owner must drain all workers and payload
borrowers before close; close refuses live allocations. A fault before
open prevents opening. Unknown non-null free or mismatched realloc
latches a fault and blocks further allocation, but permits valid
frees. Pointer address reuse cannot distinguish a stale caller pointer.
General callbacks charge GENERAL; internal callbacks charge
INTERNAL_DMA; BT flags select those classes. Unknown BT flags fail.
No fallback or class migration. These are conservative prototype
policies, not proof of full vendor allocator semantics. Zero-size
malloc/calloc fail; realloc(NULL,n) allocates; same-class realloc(p,0)
frees. Alignment is max_align_t, not verified DMA alignment.
The mock backend uses aligned_alloc on the host for all classes.
The real Linux backend must supply verified capabilities and alignment,
actual budget, lifetime and DMA ownership. In particular _get_free_heap_size
must not report an invented capacity.

Do not publish these partially populated tables or replace a live
vendor allocator piecemeal. Every pointer passed to a shared free
callback must originate from the same allocator; other allocation
sources and OSI functions must be integrated first. The BT bridge
must be the exclusive provider of its symbols. Non-multilink block
memory APIs, ISR requirements, allocation tracing, and remaining
vendor runtime dependencies are outstanding.

## Validation

Astra high read-only review confirmed the Wi-Fi and coex signatures;
flagged absent BT symbol bridge, pre-open fault handling and test
coverage. Added the bridge, compile-time profile gate, EPROTO on
pre-open fault, direct tests of all allocation slots and separate
processes for pre-open, unknown free and post-close behavior.
Final build-03 passed four normal tests and four ASan/UBSan tests
with leak detection. Main test includes 1024 concurrent allocation/
free cycles across four threads, failed realloc preservation, zeroing,
class separation, fault and close behavior. RV32IMAC ILP32 static
cross-build passed; nm confirms both BT symbols. Final corrections
have not undergone a second independent review. No physical test,
flash write, RF startup or memory reservation.

RV32 ELF SHA-256:
12faa8806040e9543365647bda603933063a4103c317de63c5c7c9819912ca5b.
Evidence: ~/deps/s31-research/radio-demand-20261127/osi-alloc-build-03.
Reproduce with:
```sh
cd experiments/s31-native-radio/resources
d="$HOME/deps/s31-research/radio-demand-20261127"
./build-radio-osi-alloc.sh "$d" "$d/osi-alloc-build-new"
```
Next integrate Linux-owned physical backing and the remaining OSI
services before attempting actual radio initialization.

## Physical Lab v1 qualification

Exact build-03 test-rv32 (177488 bytes) transferred through private
RDC transport in eight compressed chunks, each hash checked, then
reassembled and decompressed on Orion. Target RAM transfer via
115200 baud UART with 32-byte bursts and 10 ms gaps, compressed
payload verified before decompression, exact ELF SHA-256 verified
on target. No flash writes, no vendor radio libraries loaded.

Lab v1 RV32 Linux 6.18.0+, dual harts 0-1, CramFS read-only.
All four modes completed with exit status 0 and exact markers:
- S31_RADIO_OSI_ALLOC_PASS (includes 1024 concurrent allocation/free cycles)
- S31_RADIO_OSI_PREOPEN_PASS
- S31_RADIO_OSI_FOREIGN_PASS
- S31_RADIO_OSI_POSTCLOSE_PASS

Boot ID unchanged during the run, both harts still online and eth0
present. Temporary payload removed from RAM, UART released.
S31_RADIO_OSI_ALLOC_HARDWARE_PASS recorded after cleanup.
MemAvailable was 10380 KiB before upload and 9472 KiB before cleanup;
not a leak measurement, since the compressed and uncompressed files
were still present at the latter reading. The previous rollback files
on Orion passed SHA-256 checks before testing.

Private evidence: ~/deps/s31-research/radio-demand-20261127/
osi-alloc-build-03/physical-evidence (hashes verified).
Runner SHA-256 ca9bec7e139c0004762b8459e9be245fc773814fc565b68c9bfc2a4f5d109aff.
Log SHA-256 3ec37de774ae62b408933cf663a4aa73213dc54eeff7ad9fbefff5202bdd9076.
Existing UART helpers: run-uart-v4.py SHA-256
cb849e4ab7ad725ed4f3785dd581383ac6c56d701e75b361b800e0ed38234e88;
pool-stats-qa-v9.py SHA-256
3dcca231d1727022472bd6c87ab2f5d89a18286f895b226ce09ecd5b47d0a7cc.

This qualifies the allocation callbacks with a simulated backing on
actual hardware. It does not qualify real internal/DMA backing,
hardware IRQ contexts, vendor initialization, or RF. No network
traffic or full peripheral regression performed.
