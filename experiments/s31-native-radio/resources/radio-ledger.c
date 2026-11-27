/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-ledger.h"
#include <string.h>

void radio_ledger_init(struct radio_ledger *l, const size_t limits[RADIO_CLASS_COUNT], size_t internal_limit)
{
    memset(l, 0, sizeof(*l));
    for (unsigned i = 0; i < RADIO_CLASS_COUNT; ++i)
        l->usage[i].limit = limits[i];
    l->internal_limit = internal_limit;
    l->next_token = 1;
}

static enum radio_result reject(struct radio_usage *u, enum radio_result reason)
{
    if (u->rejected != UINT64_MAX)
        ++u->rejected;
    return reason;
}

enum radio_result radio_reserve(struct radio_ledger *l, enum radio_class kind,
                               size_t requested, size_t charged, uint64_t *token)
{
    if (!l || !token || (unsigned)kind >= RADIO_CLASS_COUNT ||
        !requested || charged < requested)
        return RADIO_INVALID;
    uintptr_t base = (uintptr_t)l, dest = (uintptr_t)token;
    if (dest >= base ? dest - base < sizeof(*l) : base - dest < sizeof(*token))
        return RADIO_INVALID;
    struct radio_usage *u = &l->usage[kind];
    if (charged > u->limit - u->charged)
        return reject(u, RADIO_LIMIT);
    if (kind != RADIO_GENERAL && charged > l->internal_limit - l->internal_charged)
        return reject(u, RADIO_LIMIT);
    if (!l->next_token)
        return reject(u, RADIO_EXHAUSTED);
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i) {
        struct radio_reservation *r = &l->slots[i];
        if (r->token)
            continue;
        *r = (struct radio_reservation){l->next_token++, kind, requested, charged};
        u->requested += requested;
        u->charged += charged;
        if (u->charged > u->peak)
            u->peak = u->charged;
        if (kind != RADIO_GENERAL) {
            l->internal_charged += charged;
            if (l->internal_charged > l->internal_peak)
                l->internal_peak = l->internal_charged;
        }
        *token = r->token;
        return RADIO_OK;
    }
    return reject(u, RADIO_SLOTS);
}

enum radio_result radio_release(struct radio_ledger *l, uint64_t token)
{
    if (!l || !token)
        return RADIO_INVALID;
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i) {
        struct radio_reservation *r = &l->slots[i];
        if (r->token == token) {
            struct radio_usage *u = &l->usage[r->kind];
            u->requested -= r->requested;
            u->charged -= r->charged;
            if (r->kind != RADIO_GENERAL)
                l->internal_charged -= r->charged;
            *r = (struct radio_reservation){0};
            return RADIO_OK;
        }
    }
    return RADIO_STALE;
}
