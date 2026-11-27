/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-alloc.h"
#include <string.h>

enum radio_result radio_allocator_init(struct radio_allocator *a, const struct radio_backend *b,
                                       const size_t limits[RADIO_CLASS_COUNT], size_t internal_limit)
{
    if (!a || !b || !limits || !b->quote || !b->acquire || !b->release)
        return RADIO_INVALID;
    memset(a, 0, sizeof(*a));
    a->backend = *b;
    radio_ledger_init(&a->ledger, limits, internal_limit);
    return RADIO_OK;
}

static struct radio_allocation *find(struct radio_allocator *a, uint64_t token)
{
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i)
        if (a->allocations[i].buffer.token == token)
            return &a->allocations[i];
    return NULL;
}

static int overlaps(const void *p, size_t n, const void *q, size_t m)
{
    uintptr_t x = (uintptr_t)p, y = (uintptr_t)q;
    return x <= y ? y - x < n : x - y < m;
}

static int valid_output(const struct radio_allocator *a, const struct radio_buffer *out)
{
    if (!out || overlaps(out, sizeof(*out), a, sizeof(*a)))
        return 0;
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i) {
        const struct radio_buffer *b = &a->allocations[i].buffer;
        if (b->token && overlaps(out, sizeof(*out), b->ptr, b->size))
            return 0;
    }
    return 1;
}

enum radio_result radio_malloc(struct radio_allocator *a, enum radio_class kind,
                               size_t size, size_t alignment, struct radio_buffer *out)
{
    if (!a || !valid_output(a, out) || (unsigned)kind >= RADIO_CLASS_COUNT || !size ||
        !alignment || (alignment & (alignment - 1)))
        return RADIO_INVALID;
    struct radio_allocation *slot = find(a, 0);
    if (!slot)
        return RADIO_SLOTS;
    size_t charged = 0;
    if (a->backend.quote(a->backend.context, kind, size, alignment, &charged) ||
        charged < size)
        return RADIO_BACKEND;
    uint64_t token;
    enum radio_result r = radio_reserve(&a->ledger, kind, size, charged, &token);
    if (r != RADIO_OK)
        return r;
    void *ptr = a->backend.acquire(a->backend.context, kind, size, alignment, charged);
    if (!ptr || ((uintptr_t)ptr & (alignment - 1))) {
        if (ptr)
            a->backend.release(a->backend.context, kind, ptr, charged);
        radio_release(&a->ledger, token);
        return RADIO_BACKEND;
    }
    *slot = (struct radio_allocation){{token, ptr, size}, kind, alignment, charged};
    *out = slot->buffer;
    return RADIO_OK;
}

enum radio_result radio_calloc(struct radio_allocator *a, enum radio_class kind,
                               size_t count, size_t size, size_t alignment, struct radio_buffer *out)
{
    if (!count || !size || count > SIZE_MAX / size)
        return RADIO_INVALID;
    enum radio_result r = radio_malloc(a, kind, count * size, alignment, out);
    if (r == RADIO_OK)
        memset(out->ptr, 0, out->size);
    return r;
}

enum radio_result radio_free(struct radio_allocator *a, uint64_t token)
{
    if (!a || !token)
        return RADIO_INVALID;
    struct radio_allocation *slot = find(a, token);
    if (!slot)
        return RADIO_STALE;
    a->backend.release(a->backend.context, slot->kind, slot->buffer.ptr, slot->charged);
    enum radio_result r = radio_release(&a->ledger, token);
    *slot = (struct radio_allocation){0};
    return r;
}

enum radio_result radio_realloc(struct radio_allocator *a, uint64_t token,
                                size_t size, struct radio_buffer *out)
{
    if (!a || !token || !size || !valid_output(a, out))
        return RADIO_INVALID;
    struct radio_allocation *old = find(a, token);
    if (!old)
        return RADIO_STALE;
    struct radio_buffer new_buffer;
    enum radio_result r = radio_malloc(a, old->kind, size, old->alignment, &new_buffer);
    if (r != RADIO_OK)
        return r;
    memcpy(new_buffer.ptr, old->buffer.ptr,
           size < old->buffer.size ? size : old->buffer.size);
    /* Under the serialized, valid-backend contract this cannot fail. */
    r = radio_free(a, token);
    if (r != RADIO_OK) {
        radio_free(a, new_buffer.token);
        return r;
    }
    *out = new_buffer;
    return RADIO_OK;
}
