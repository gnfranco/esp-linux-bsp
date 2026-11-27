/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_RADIO_ALLOC_H
#define S31_RADIO_ALLOC_H
#include "radio-ledger.h"

/* Process-context prototype, externally serialized, no callback reentry.
 * Backend quote is pure and must include all charged backing bytes; acquire
 * returns unique nonoverlapping CPU-accessible storage of at least requested
 * bytes, with the specified alignment and class, or NULL with no ownership
 * transferred. release is synchronous and infallible for a valid allocation.
 * No active hardware or DMA, no implicit fallback, no pointer-based free.
 * A future kernel backend must establish ownership and quiescence separately.
 * All tokens are scoped to one live allocator. Drain all before reinit.
 * Configuration must not alias allocator state. Output must be writable
 * for the full operation and disjoint from allocator state and every live
 * payload, especially the buffer released by realloc.
 * Peaks record reservations (including failed backend acquisitions),
 * not exclusively successful physical allocations.
 */
struct radio_backend {
    void *context;
    int (*quote)(void *, enum radio_class, size_t, size_t, size_t *);
    void *(*acquire)(void *, enum radio_class, size_t, size_t, size_t);
    void (*release)(void *, enum radio_class, void *, size_t);
};
struct radio_buffer {
    uint64_t token;
    void *ptr;
    size_t size;
};
struct radio_allocation {
    struct radio_buffer buffer;
    enum radio_class kind;
    size_t alignment, charged;
};
struct radio_allocator {
    struct radio_ledger ledger;
    struct radio_backend backend;
    struct radio_allocation allocations[RADIO_LEDGER_SLOTS];
};
enum radio_result radio_allocator_init(struct radio_allocator *, const struct radio_backend *,
                                       const size_t limits[RADIO_CLASS_COUNT], size_t internal_limit);
enum radio_result radio_malloc(struct radio_allocator *, enum radio_class,
                               size_t size, size_t alignment, struct radio_buffer *out);
enum radio_result radio_calloc(struct radio_allocator *, enum radio_class,
                               size_t count, size_t size, size_t alignment, struct radio_buffer *out);
/* Same-class copy-and-release; temporarily charges BOTH buffers.
 * Failure leaves old allocation and output unchanged. Zero size rejected.
 * On success old token and pointer expire. Caller must drain all borrowers
 * before free/realloc; no automatic synchronization with users of storage.
 */
enum radio_result radio_realloc(struct radio_allocator *, uint64_t token,
                                size_t size, struct radio_buffer *out);
enum radio_result radio_free(struct radio_allocator *, uint64_t token);
#endif
