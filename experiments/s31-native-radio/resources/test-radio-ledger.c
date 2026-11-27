/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-ledger.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    struct radio_ledger l;
    uint64_t a, b, c, t = 99;
    /* Synthetic limits only; not a physical SRAM partition. */
    const size_t limits[] = {65536, 4096, 8192};
    radio_ledger_init(&l, limits, 8192);
    CHECK(radio_reserve(&l, RADIO_INTERNAL_DMA, 1600, 2048, &a) == RADIO_OK);
    CHECK(radio_reserve(&l, RADIO_INTERNAL_DMA, 1600, 2048, &b) == RADIO_OK);
    CHECK(l.usage[RADIO_INTERNAL_DMA].requested == 3200);
    CHECK(l.usage[RADIO_INTERNAL_DMA].charged == 4096);
    CHECK(radio_reserve(&l, RADIO_INTERNAL_DMA, 4097, 4097, &t) == RADIO_LIMIT && t == 99);
    CHECK(radio_reserve(&l, RADIO_INTERNAL, 4097, 4097, &t) == RADIO_LIMIT);
    CHECK(radio_reserve(&l, RADIO_GENERAL, 4097, 8192, &c) == RADIO_OK);
    CHECK(radio_release(&l, a) == RADIO_OK);
    CHECK(radio_release(&l, a) == RADIO_STALE);
    CHECK(radio_reserve(&l, RADIO_INTERNAL_DMA, 4096, 4096, &t) == RADIO_OK && t != a);
    CHECK(l.usage[RADIO_INTERNAL_DMA].peak == 6144);
    CHECK(radio_release(&l, a) == RADIO_STALE);
    CHECK(radio_reserve(&l, RADIO_INTERNAL, 2049, 2049, &a) == RADIO_LIMIT);
    CHECK(radio_reserve(&l, RADIO_INTERNAL, 2048, 2048, &a) == RADIO_OK);
    CHECK(l.internal_charged == 8192 && l.internal_peak == 8192);
    CHECK(radio_release(&l, a) == RADIO_OK);
    CHECK(radio_release(&l, b) == RADIO_OK && radio_release(&l, t) == RADIO_OK);
    CHECK(l.usage[RADIO_INTERNAL_DMA].charged == 0 && l.usage[RADIO_INTERNAL_DMA].peak == 6144);
    CHECK(radio_release(&l, c) == RADIO_OK);
    CHECK(l.internal_charged == 0 && l.internal_peak == 8192);
    CHECK(radio_reserve(&l, RADIO_GENERAL, 0, 1, &t) == RADIO_INVALID);
    CHECK(radio_reserve(&l, RADIO_GENERAL, 2, 1, &t) == RADIO_INVALID);
    CHECK(radio_reserve(&l, RADIO_GENERAL, SIZE_MAX, SIZE_MAX, &t) == RADIO_LIMIT);
    CHECK(radio_reserve(&l, RADIO_CLASS_COUNT, 1, 1, &t) == RADIO_INVALID);
    uint64_t all[RADIO_LEDGER_SLOTS];
    for (unsigned i=0; i<RADIO_LEDGER_SLOTS; ++i)
        CHECK(radio_reserve(&l, RADIO_GENERAL, 1, 1, &all[i]) == RADIO_OK);
    CHECK(radio_reserve(&l, RADIO_GENERAL, 1, 1, &t) == RADIO_SLOTS);
    for (unsigned i=0; i<RADIO_LEDGER_SLOTS; ++i)
        CHECK(radio_release(&l, all[i]) == RADIO_OK);
    CHECK(l.usage[RADIO_GENERAL].charged == 0);
    /* Force terminal token state in test; the API never wraps to a live ID. */
    l.next_token = UINT64_MAX;
    CHECK(radio_reserve(&l, RADIO_GENERAL, 1, 1, &t) == RADIO_OK && t == UINT64_MAX);
    CHECK(radio_release(&l, t) == RADIO_OK);
    CHECK(radio_reserve(&l, RADIO_GENERAL, 1, 1, &t) == RADIO_EXHAUSTED);
    const size_t closed[] = {0,0,0};
    radio_ledger_init(&l, closed, 0);
    CHECK(radio_reserve(&l, RADIO_INTERNAL_DMA, 1, 1, &t) == RADIO_LIMIT);
    puts("S31_RADIO_LEDGER_PASS");
    return 0;
}
