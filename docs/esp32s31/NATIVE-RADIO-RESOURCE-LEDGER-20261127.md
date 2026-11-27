# Native radio resource ledger and demand characterization

Date: 2026-11-27. Development milestone, not final hardware
qualification. Keep the UART native port and sustained peripheral
regression separate. The original Lab v1 remains the reference.

## Vendor source findings

Source basis: ESP-IDF v6.1 commit
fff9895c82d744c7237be8847347bdd1b07c6643. Retrieved blobs by
the pinned tree and independently checked their Git hashes.
Private copies and source manifests: ~/deps/s31-research/radio-demand-20261127.

- Wi-Fi Kconfig c8803d747f5f3c51242ad2145b843585b0928ce2:
  static RX defaults to 10 or 16 with the PSRAM-preferred, nonoptional
  configuration; static TX defaults to 16, dynamic TX to 32,
  dynamic RX to 32, static management RX to 5. The PSRAM-preferred
  nonoptional condition excludes dynamic TX. The documented sizes
  are approximate: 1.6 KB each for static data buffers and 500 B
  each for static management RX. For illustrative decimal 1600 B,
  the PSRAM-preferred static profile accounts for 16*1600 +
  16*1600 + 5*500 = 53700 B (~52.4 KiB) of nominal static buffer
  storage. The dynamic TX profile has 10*1600 + 5*500 = 18500 B
  (~18.1 KiB) nominal static storage, plus variable TX/RX demand.
  These are not measured allocations, guaranteed minima, SRAM-only
  budgets or recommended final configurations. The source does
  not quantify allocator overhead, all frame sizes, actual selected
  library sections, queues, stacks, descriptors, PHY or coexistence.
- Wi-Fi adapter 48b2ba9dc31037f74b22917664423321a16af8d2
  and coexistence adapter ec8eddb75cdb0ef645fbb456e1ad8dec1a7b1555:
  internal allocation paths require INTERNAL|DMA|8BIT.
  Static queue and mux objects may require INTERNAL|8BIT without
  DMA. General Wi-Fi allocation may prefer PSRAM with internal
  fallback on IDF; no implicit fallback allowed in the new ledger.
- Bluetooth controller bt.c c8fc8f4f863938b08f2b3b557b13b22a85728e36:
  initial counts are 103 events, 3 event queues, 10 callouts,
  zero semaphores and one mutex, before BLE/BR-EDR calculations.
  The actual final counts depend on controller configuration.
  btdm_osal_freertos.c b9220767d1879d56a4694bb63bb5e2ea678a56a9:
  explicit internal allocation uses INTERNAL|8BIT|DMA;
  general allocation uses 8BIT. Conditional BTDM_MEMPOOL_ALLOC
  path allocates the event, queue, callout, semaphore and mutex
  pools through that internal wrapper. btdm_mempool.h
  32b49a7b38d5f0fc20268045708d30a2d447ee36 defines a four-byte
  aligned block size: round_up(sizeof(element),4)*count. This
  is not a byte total until element sizes and enabled pools
  for the selected controller have been established. The
  non-multilink memory-block wrapper also charges 4 bytes of
  header. Multilink is default y in S31 controller Kconfig
  9cf41b57f2a5b30da3de659d0c144c0948d9310c.
- PHY calibration type in esp_phy_init.h
  c41e970c5a69ed145130ce550143df72d193d7bb is 4+6+1894 = 1904 B.
  phy_init.c dc2dbf0dccf9f2bfd6c58171a7a13aaaf4523b12 allocates
  it with ordinary calloc. S31 soc_caps.h
  53e2aec61a3cc50356e52696e5dca0dc40c00f71 selects modem retention
  by REGDMA, not the BACKUPDMA branch; do not charge a backup
  buffer from an unrelated target. Remaining PHY requests unknown.

### Timer ABI follow-up

Found actual ESP-IDF implementation in components/esp_timer/src/
ets_timer_legacy.c (blob 1bf801e53053c3048462ee8453d26ff0a1cac642),
included in the esp_timer CMake source list (8ff75b2c1b665d514060a56c09d2c7641a56c055).
It creates ESP_TIMER_TASK-dispatched callbacks, stores an internal
handle in timer_arg, initializes a magic in timer_expire, treats
repeated setfn on an existing handle as no-op, stops before arm,
and deletes on done. esp_timer.c cf8d8abd83c1a260c39e571c5fc4ff4dabd57854
normally advances periodic alarms by period, not callback completion;
skip_unhandled_events is false by default in the legacy creation
args. The existing typed Linux binding is a fixed-delay prototype,
not an ABI-equivalent implementation. Preserve it as a tested
primitive; next vendor adapter must explicitly reconcile semantics.
No timer changes in this branch.

## First logical resource-management component

radio-ledger.{h,c}: bounded reservation ledger, three disjoint
request classes (GENERAL, INTERNAL, INTERNAL_DMA), individual caps,
shared aggregate cap for the two internal classes, requested vs
charged bytes, per-class and aggregate peak charged bytes, per-class
saturating rejection counts. Zero budget denies requests.
No hidden fallback. Charge is explicitly supplied by the backend
and must be >= requested size. Tokens are nonzero and monotonic;
at wrap the ledger refuses new reservations. Reject stale release.
64 slots is a prototype bound, not a radio configuration guarantee.
Caller must serialize access, supply valid initialized storage and
nonaliasing outputs, and drain tokens before reinitialization.
The GENERAL backing must be independent of internal SRAM. If the
actual backend cannot guarantee this, it must not use that class.
The ledger does not provide fragmentation, address alignment,
physical ownership, actual memory, realloc semantics, allocation
rollback, IRQ safety, vendor ABI or a kernel interface.

The real allocation budget remains UNKNOWN. The 236 KiB observed
free and 128 KiB largest aligned block belong to the Linux global
pool, not to the radio. A backend must be owned by Linux, reserve
actual extents, use appropriate device DMA mappings and maintain
headroom for existing consumers before assigning any budget.

## Validation

Synthetic reservation tests check per-class and combined limits,
requested/charged values, peak retention, rejection without replacing
the output token, stale releases, slot exhaustion, token wrap,
zero limit and invalid input. No vendor firmware, physical access,
new flash writes or board testing. Test limits are synthetic.

Reproduce from experiments/s31-native-radio/resources with a fresh
output directory:
```sh
cc -std=c11 -O2 -Wall -Wextra -Werror radio-ledger.c test-radio-ledger.c -o /tmp/test-radio-ledger
/tmp/test-radio-ledger
```
Final build-03: normal host test, non-PIE ASan/UBSan with leak check,
and separate UBSan passed. A prior PIE sanitizer run failed with
DEADLYSIGNAL and no actionable report; cause not established, do
not claim that the original invocation was successful. RV32IMAC
ILP32 static ELF compiled using the qualified GCC 14.1.1 toolchain;
not executed on target. All four artifacts hash-verified in
~/deps/s31-research/radio-demand-20261127/build-03/SHA256SUMS.
Full physical regression intentionally deferred to integration stage.

Next: connect a typed allocation adapter to an explicit backend,
record real requests/frees and their lifetimes, support transactional
failure/realloc, and implement Linux-owned DMA resource backing.
Do not start RF or silently treat internal-required allocations as
ordinary userspace heap. No upstream submission yet.

Independent Astra high read-only review of the three ledger files:
no blockers found under the stated serialized caller contract.
This review does not qualify a future backend or physical SRAM mapping.
