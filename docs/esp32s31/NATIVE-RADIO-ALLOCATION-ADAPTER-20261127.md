# Native radio allocation adapter — experimental milestone

Date: 2026-11-27. Parent: 8e57c961d60ef4c2c8790d717b537a355943c2be.
This is the next step after the resource ledger. It is an independently
reviewable, process-context prototype, not a Linux kernel DMA interface
or a vendor OSI ABI. There is no physical SRAM reservation or RF startup.

## Ownership and transactional behavior

The explicit backend supplies a side-effect-free charge quote, a
class-preserving acquisition and an infallible synchronous release.
The adapter reserves the quoted bytes before acquiring; if acquisition
fails it removes the reservation and does not change the caller output.
Reservation peaks intentionally include attempts that reach the backend.
No implicit cross-class fallback. The mock backend uses host
aligned_alloc for every synthetic class; this is not evidence that
ordinary Linux heap satisfies internal or DMA capabilities.

Malloc checks nonzero size and power-of-two alignment. Calloc rejects
multiplication overflow and clears requested bytes. Realloc preserves
class and alignment; allocate and copy first, then release the old
buffer. On allocation failure, old contents and output remain unchanged.
Both allocations count against the budget during the transaction,
including a shrinking realloc. Zero-size requests are rejected;
NULL-pointer and zero-size vendor ABI semantics remain to be mapped.
Tokens are scoped to one allocator lifetime. Stale tokens are rejected,
including after realloc. The caller serializes the whole API, drains
borrowers before freeing, and must not reinitialize with live tokens.
Backend callback reentry and hardware DMA ownership are unsupported.

## Review and validation

Astra high read-only review identified output aliasing with freed
payload and ledger token output aliasing its own next-token counter,
plus test gaps around quote overflow, undercharging and exhaustion.
Corrected by explicitly rejecting overlapping output storage and
documenting lifetime contracts. Added tests for those cases.
Final implementation has not received a second independent review.

Build-02 completed:
- normal host allocation test PASS;
- non-PIE ASan/UBSan with leak detection PASS;
- original ledger regression PASS;
- static RV32IMAC ILP32 cross build PASS, not executed on board.

RV32 ELF SHA-256:
e1145e911533047af9e1c419007062b7c4c63dc9af2bed6dbfd0df009b17a404.
All artifacts checked against SHA256SUMS in
~/deps/s31-research/radio-demand-20261127/adapter-build-02.

To reproduce from experiments/s31-native-radio/resources:
```sh
cc -std=c11 -O2 -Wall -Wextra -Werror \
  radio-ledger.c radio-alloc.c test-radio-alloc.c -o /tmp/test-radio-alloc
/tmp/test-radio-alloc
```

## Next gate

Wire a typed vendor allocation adapter with pointer-to-token ownership,
explicit zero-size/realloc(NULL) behavior, synchronization and request
tracing. The real kernel backend must establish DMA mapping, physical
ownership, release/quiescence and budget from verified resources.
No claim of a physical allocator or vendor initialization, no change
to the board image. Per user decision, defer full peripheral regression
to the integrated-platform qualification.
