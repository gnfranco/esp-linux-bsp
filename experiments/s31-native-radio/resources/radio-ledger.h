/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_RADIO_LEDGER_H
#define S31_RADIO_LEDGER_H
#include <stddef.h>
#include <stdint.h>

/* Logical accounting only. No backing storage, pointers, DMA or MMIO.
 * Caller owns and serializes the ledger, including init and snapshots.
 * Budgets are explicit policy inputs, not inferred from free SRAM.
 * INTERNAL and INTERNAL_DMA share an aggregate internal limit;
 * GENERAL must not be backed by that same physical pool.
 * A reservation is charged before a backend allocation; release only
 * after the backend has relinquished the storage. No cross-class fallback.
 * Init limits must not alias ledger storage. Reservation output must
 * be valid writable storage disjoint from the ledger.
 * Never reinitialize while any reservation token might still be used.
 */
enum radio_class { RADIO_GENERAL, RADIO_INTERNAL, RADIO_INTERNAL_DMA, RADIO_CLASS_COUNT };
enum radio_result { RADIO_OK, RADIO_INVALID, RADIO_LIMIT, RADIO_SLOTS, RADIO_STALE, RADIO_EXHAUSTED, RADIO_BACKEND };
#define RADIO_LEDGER_SLOTS 64
struct radio_usage {
    size_t limit, requested, charged, peak;
    uint64_t rejected;
};
struct radio_reservation {
    uint64_t token;
    enum radio_class kind;
    size_t requested, charged;
};
struct radio_ledger {
    struct radio_usage usage[RADIO_CLASS_COUNT];
    struct radio_reservation slots[RADIO_LEDGER_SLOTS];
    size_t internal_limit, internal_charged, internal_peak;
    uint64_t next_token;
};
void radio_ledger_init(struct radio_ledger *, const size_t limits[RADIO_CLASS_COUNT], size_t internal_limit);
enum radio_result radio_reserve(struct radio_ledger *, enum radio_class,
                               size_t requested, size_t charged, uint64_t *token);
enum radio_result radio_release(struct radio_ledger *, uint64_t token);
#endif
