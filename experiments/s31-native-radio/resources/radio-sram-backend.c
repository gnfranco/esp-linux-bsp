/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-sram-backend.h"
#include <stdlib.h>
#include <string.h>

static int quote(void *context, enum radio_class kind, size_t n, size_t a,
                 size_t *charged)
{
    (void)context;
    if (!n || !a || (a & (a - 1)) || (unsigned)kind >= RADIO_CLASS_COUNT)
        return -1;
    size_t quantum = kind == RADIO_GENERAL ? a : S31_SRAM_UNIT;
    if (kind != RADIO_GENERAL && (a > 4096 || n > S31_SRAM_BYTES))
        return -1;
    if (n > SIZE_MAX - (quantum - 1))
        return -1;
    *charged = (n + quantum - 1) & ~(quantum - 1);
    return 0;
}

static void *acquire(void *context, enum radio_class kind, size_t n, size_t a,
                     size_t charged)
{
    struct radio_sram_backend *s = context;
    size_t expected;
    if (quote(s, kind, n, a, &expected) || expected != charged)
        return NULL;
    if (kind == RADIO_GENERAL) {
        if (charged > SIZE_MAX - s->live_general)
            return NULL;
        void *p = aligned_alloc(a, charged);
        if (p) {
            s->live_general += charged;
            ++s->live_blocks;
        }
        return p;
    }
    size_t units = charged / S31_SRAM_UNIT;
    for (size_t i = 0; i <= S31_SRAM_UNITS - units; ++i) {
        unsigned char *p = s->base + i * S31_SRAM_UNIT;
        if ((uintptr_t)p & (a - 1))
            continue;
        size_t j = 0;
        while (j < units && !s->occupied[i + j])
            ++j;
        if (j != units)
            continue;
        memset(s->occupied + i, 1, units);
        s->live_internal += charged;
        if (s->live_internal > s->peak_internal)
            s->peak_internal = s->live_internal;
        ++s->live_blocks;
        return p;
    }
    return NULL;
}

static void release(void *context, enum radio_class kind, void *p, size_t charged)
{
    struct radio_sram_backend *s = context;
    /* Valid allocations only; impossible ledger/backend mismatch fails shut. */
    if (!p || !charged || !s->live_blocks)
        abort();
    if (kind == RADIO_GENERAL) {
        if (charged > s->live_general)
            abort();
        s->live_general -= charged;
        free(p);
    } else {
        uintptr_t addr = (uintptr_t)p, base = (uintptr_t)s->base;
        if ((unsigned)kind >= RADIO_CLASS_COUNT || addr < base ||
            addr - base >= S31_SRAM_BYTES || (addr - base) % S31_SRAM_UNIT ||
            charged % S31_SRAM_UNIT || charged > S31_SRAM_BYTES - (addr - base) ||
            charged > s->live_internal)
            abort();
        size_t i = (addr - base) / S31_SRAM_UNIT;
        size_t units = charged / S31_SRAM_UNIT;
        for (size_t j = 0; j < units; ++j)
            if (!s->occupied[i + j])
                abort();
        memset(p, 0, charged);
        memset(s->occupied + i, 0, units);
        s->live_internal -= charged;
    }
    --s->live_blocks;
}

int radio_sram_init(struct radio_sram_backend *s, void *mapping, size_t bytes,
                    struct radio_backend *b)
{
    if (!s || !b || !mapping || bytes != S31_SRAM_BYTES ||
        ((uintptr_t)mapping & 4095u))
        return -1;
    memset(s, 0, sizeof(*s));
    s->base = mapping;
    *b = (struct radio_backend){s, quote, acquire, release};
    return 0;
}

int radio_sram_idle(const struct radio_sram_backend *s)
{
    if (!s || !s->base || s->live_blocks || s->live_general || s->live_internal)
        return 0;
    for (size_t i = 0; i < S31_SRAM_UNITS; ++i)
        if (s->occupied[i])
            return 0;
    return 1;
}
