/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-osi-alloc.h"
#include <pthread.h>
#include <errno.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct radio_allocator arena;
static enum { FRESH, OPEN, CLOSED } state;
static int fault;

static struct radio_allocation *lookup(const void *p)
{
    for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i)
        if (arena.allocations[i].buffer.token && arena.allocations[i].buffer.ptr == p)
            return &arena.allocations[i];
    return NULL;
}

int radio_osi_open(const struct radio_backend *b, const size_t limits[RADIO_CLASS_COUNT],
                   size_t internal_limit)
{
    pthread_mutex_lock(&lock);
    int rc = EBUSY;
    if (state == FRESH && fault)
        rc = EPROTO;
    else if (state == FRESH) {
        rc = EINVAL;
        if (radio_allocator_init(&arena, b, limits, internal_limit) == RADIO_OK) {
            state = OPEN;
            rc = 0;
        }
    }
    pthread_mutex_unlock(&lock);
    return rc;
}

int radio_osi_close(void)
{
    pthread_mutex_lock(&lock);
    int rc = EINVAL;
    if (state == OPEN) {
        rc = 0;
        for (unsigned i = 0; i < RADIO_LEDGER_SLOTS; ++i)
            if (arena.allocations[i].buffer.token)
                rc = EBUSY;
        if (!rc)
            state = CLOSED;
    }
    pthread_mutex_unlock(&lock);
    return rc;
}

int radio_osi_faulted(void)
{
    pthread_mutex_lock(&lock);
    int value = fault;
    pthread_mutex_unlock(&lock);
    return value;
}

static void *alloc(enum radio_class kind, size_t count, size_t size, int zero)
{
    struct radio_buffer b;
    void *p = NULL;
    pthread_mutex_lock(&lock);
    if (state == OPEN && !fault) {
        enum radio_result r = zero ?
            radio_calloc(&arena, kind, count, size, _Alignof(max_align_t), &b) :
            radio_malloc(&arena, kind, size, _Alignof(max_align_t), &b);
        if (r == RADIO_OK)
            p = b.ptr;
    }
    pthread_mutex_unlock(&lock);
    return p;
}

void radio_osi_free(void *p)
{
    if (!p)
        return;
    pthread_mutex_lock(&lock);
    struct radio_allocation *a = state == OPEN ? lookup(p) : NULL;
    if (!a || radio_free(&arena, a->buffer.token) != RADIO_OK)
        fault = 1;
    pthread_mutex_unlock(&lock);
}

static void *resize(enum radio_class kind, void *p, size_t size)
{
    if (!p)
        return alloc(kind, 1, size, 0);
    pthread_mutex_lock(&lock);
    struct radio_allocation *a = state == OPEN ? lookup(p) : NULL;
    void *result = NULL;
    if (!a || a->kind != kind)
        fault = 1;
    else if (!size) {
        if (radio_free(&arena, a->buffer.token) != RADIO_OK)
            fault = 1;
    } else if (!fault) {
        struct radio_buffer b;
        if (radio_realloc(&arena, a->buffer.token, size, &b) == RADIO_OK)
            result = b.ptr;
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static void *general_malloc(size_t n) { return alloc(RADIO_GENERAL, 1, n, 0); }
static void *general_calloc(size_t n, size_t s) { return alloc(RADIO_GENERAL, n, s, 1); }
static void *general_zalloc(size_t n) { return general_calloc(1, n); }
static void *general_realloc(void *p, size_t n) { return resize(RADIO_GENERAL, p, n); }
static void *internal_malloc(size_t n) { return alloc(RADIO_INTERNAL_DMA, 1, n, 0); }
static void *internal_calloc(size_t n, size_t s) { return alloc(RADIO_INTERNAL_DMA, n, s, 1); }
static void *internal_zalloc(size_t n) { return internal_calloc(1, n); }
static void *internal_realloc(void *p, size_t n) { return resize(RADIO_INTERNAL_DMA, p, n); }

void *radio_osi_bt_malloc(uint32_t n, btdm_osal_malloc_flag_t flags)
{
    switch (flags) {
    case BTDM_OSAL_MALLOC_F_INTERNAL: return internal_malloc(n);
    case BTDM_OSAL_MALLOC_F_NORMAL: return general_malloc(n);
    default: return NULL;
    }
}

int radio_osi_wire_alloc(wifi_osi_funcs_t *w, coex_adapter_funcs_t *c)
{
    if (!w || !c)
        return EINVAL;
    w->_malloc = general_malloc;
    w->_free = radio_osi_free;
    w->_malloc_internal = internal_malloc;
    w->_calloc_internal = internal_calloc;
    w->_zalloc_internal = internal_zalloc;
    w->_realloc_internal = internal_realloc;
    w->_wifi_malloc = general_malloc;
    w->_wifi_calloc = general_calloc;
    w->_wifi_zalloc = general_zalloc;
    w->_wifi_realloc = general_realloc;
    c->_malloc_internal = internal_malloc;
    c->_free = radio_osi_free;
    return 0;
}
