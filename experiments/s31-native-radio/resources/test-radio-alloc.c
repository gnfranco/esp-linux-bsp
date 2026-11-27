/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
struct fake {
    unsigned acquire_calls, release_calls;
    size_t live[RADIO_CLASS_COUNT];
    int fail, bad_quote;
};
static int quote(void *ctx, enum radio_class kind, size_t size, size_t alignment, size_t *charged)
{
    struct fake *f = ctx;
    (void)kind;
    if (f->bad_quote == 2) {
        *charged = size - 1;
        return 0;
    }
    if (f->bad_quote || alignment < sizeof(void *) || size > SIZE_MAX - (alignment - 1))
        return -1;
    *charged = (size + alignment - 1) & ~(alignment - 1);
    return 0;
}
static void *acquire(void *ctx, enum radio_class kind, size_t size, size_t alignment, size_t charged)
{
    struct fake *f = ctx;
    (void)size;
    ++f->acquire_calls;
    if (f->fail)
        return NULL;
    void *p = aligned_alloc(alignment, charged);
    if (p) {
        memset(p, 0xa5, charged);
        f->live[kind] += charged;
    }
    return p;
}
static void release(void *ctx, enum radio_class kind, void *p, size_t charged)
{
    struct fake *f = ctx;
    if (charged > f->live[kind])
        abort();
    f->live[kind] -= charged;
    ++f->release_calls;
    free(p);
}
int main(void)
{
    struct fake f = {0};
    struct radio_backend b = {&f, quote, acquire, release};
    struct radio_allocator a;
    const size_t limits[] = {4096, 1024, 1024};
    CHECK(radio_allocator_init(&a, &b, limits, 1024) == RADIO_OK);
    struct radio_buffer x = {0}, y = {0}, out = {99, NULL, 77};
    CHECK(radio_calloc(&a, RADIO_INTERNAL_DMA, 3, 100, 64, &x) == RADIO_OK);
    CHECK(x.size == 300 && ((uintptr_t)x.ptr % 64) == 0);
    for (size_t i = 0; i < x.size; ++i)
        CHECK(((unsigned char *)x.ptr)[i] == 0);
    memset(x.ptr, 0x5c, x.size);
    CHECK(radio_realloc(&a, x.token, 512, x.ptr) == RADIO_INVALID);
    CHECK(radio_malloc(&a, RADIO_GENERAL, 64, 64, x.ptr) == RADIO_INVALID);
    CHECK(radio_malloc(&a, RADIO_GENERAL, 64, 64, &a.allocations[0].buffer) == RADIO_INVALID);
    CHECK(radio_reserve(&a.ledger, RADIO_GENERAL, 1, 1, &a.ledger.next_token) == RADIO_INVALID);
    CHECK(a.ledger.next_token == x.token + 1);
    CHECK(a.ledger.internal_charged == 320 && f.live[RADIO_INTERNAL_DMA] == 320);
    CHECK(radio_malloc(&a, RADIO_INTERNAL, 750, 64, &out) == RADIO_LIMIT);
    CHECK(out.token == 99 && out.size == 77 && out.ptr == NULL);
    CHECK(f.acquire_calls == 1);
    CHECK(radio_malloc(&a, RADIO_INTERNAL, 256, 64, &y) == RADIO_OK);
    CHECK(radio_realloc(&a, x.token, 512, &out) == RADIO_LIMIT);
    CHECK(out.token == 99 && ((unsigned char *)x.ptr)[299] == 0x5c);
    CHECK(radio_free(&a, y.token) == RADIO_OK);
    f.fail = 1;
    CHECK(radio_realloc(&a, x.token, 512, &out) == RADIO_BACKEND);
    CHECK(out.token == 99 && a.ledger.internal_charged == 320);
    CHECK(((unsigned char *)x.ptr)[299] == 0x5c);
    f.fail = 0;
    uint64_t old = x.token;
    CHECK(radio_realloc(&a, x.token, 512, &x) == RADIO_OK);
    CHECK(x.token != old && x.size == 512 && a.ledger.internal_charged == 512);
    for (size_t i = 0; i < 300; ++i)
        CHECK(((unsigned char *)x.ptr)[i] == 0x5c);
    CHECK(radio_free(&a, old) == RADIO_STALE);
    CHECK(radio_realloc(&a, x.token, 80, &x) == RADIO_OK);
    CHECK(x.size == 80 && a.ledger.internal_charged == 128);
    for (size_t i = 0; i < 80; ++i)
        CHECK(((unsigned char *)x.ptr)[i] == 0x5c);
    CHECK(radio_free(&a, x.token) == RADIO_OK);
    CHECK(a.ledger.internal_charged == 0 && a.ledger.internal_peak == 832);
    CHECK(radio_calloc(&a, RADIO_GENERAL, SIZE_MAX, 2, 64, &out) == RADIO_INVALID);
    CHECK(radio_malloc(&a, RADIO_GENERAL, 1, 3, &out) == RADIO_INVALID);
    CHECK(radio_malloc(&a, RADIO_CLASS_COUNT, 1, 64, &out) == RADIO_INVALID);
    f.bad_quote = 1;
    CHECK(radio_malloc(&a, RADIO_GENERAL, 1, 64, &out) == RADIO_BACKEND);
    f.bad_quote = 2;
    CHECK(radio_malloc(&a, RADIO_GENERAL, 1, 64, &out) == RADIO_BACKEND);
    f.bad_quote = 0;
    CHECK(radio_malloc(&a, RADIO_GENERAL, SIZE_MAX, 64, &out) == RADIO_BACKEND);
    CHECK(out.token == 99);
    struct radio_buffer all[RADIO_LEDGER_SLOTS];
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i)
        CHECK(radio_malloc(&a, RADIO_GENERAL, 1, 64, &all[i]) == RADIO_OK);
    CHECK(radio_malloc(&a, RADIO_GENERAL, 1, 64, &out) == RADIO_SLOTS);
    CHECK(radio_realloc(&a, all[0].token, 2, &out) == RADIO_SLOTS);
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i)
        CHECK(radio_free(&a, all[i].token) == RADIO_OK);
    for (unsigned i = 0; i < RADIO_CLASS_COUNT; ++i)
        CHECK(f.live[i] == 0 && a.ledger.usage[i].charged == 0);
    CHECK(f.acquire_calls == f.release_calls + 1);
    CHECK(radio_allocator_init(&a, &b, limits, 1024) == RADIO_OK);
    a.ledger.next_token = UINT64_MAX;
    f.fail = 1;
    CHECK(radio_malloc(&a, RADIO_INTERNAL_DMA, 1, 64, &out) == RADIO_BACKEND);
    CHECK(out.token == 99 && a.ledger.internal_charged == 0 && a.ledger.internal_peak == 64);
    CHECK(f.live[RADIO_INTERNAL_DMA] == 0);
    f.fail = 0;
    CHECK(radio_malloc(&a, RADIO_INTERNAL_DMA, 1, 64, &out) == RADIO_EXHAUSTED);
    puts("S31_RADIO_ALLOC_PASS");
    return 0;
}
