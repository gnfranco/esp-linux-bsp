/* SPDX-License-Identifier: Apache-2.0 */
#include "osi-timer-binding.h"
#include <pthread.h>
#include <stdlib.h>

struct entry {
    const void *key;
    struct s31_timer_id id;
    bool retiring;
};
struct s31_timer_binding {
    pthread_mutex_t lock;
    s31_timer_service *service;
    uint32_t capacity;
    bool closed;
    struct entry entries[];
};

enum s31_timer_result s31_binding_create(uint32_t capacity, size_t stack_size,
                                        s31_timer_binding **out)
{
    s31_timer_binding *b;
    enum s31_timer_result rc;
    if (!out)
        return S31_TIMER_INVALID;
    *out = NULL;
    if (!capacity || capacity > S31_TIMER_MAX_SLOTS)
        return S31_TIMER_INVALID;
    b = calloc(1, sizeof(*b) + capacity * sizeof(b->entries[0]));
    if (!b)
        return S31_TIMER_SYSTEM;
    if (pthread_mutex_init(&b->lock, NULL)) {
        free(b);
        return S31_TIMER_SYSTEM;
    }
    rc = s31_timer_service_create(capacity, stack_size, &b->service);
    if (rc != S31_TIMER_OK) {
        pthread_mutex_destroy(&b->lock);
        free(b);
        return rc;
    }
    b->capacity = capacity;
    *out = b;
    return S31_TIMER_OK;
}

static struct entry *lookup(s31_timer_binding *b, const void *key)
{
    for (uint32_t i = 0; i < b->capacity; i++)
        if (b->entries[i].key == key)
            return &b->entries[i];
    return NULL;
}

static enum s31_timer_result finish(s31_timer_binding *b,
                                    enum s31_timer_result rc)
{
    if (rc == S31_TIMER_SYSTEM || rc == S31_TIMER_WORKER_FAILED) {
        b->closed = true;
        (void)s31_timer_service_close(b->service);
    }
    pthread_mutex_unlock(&b->lock);
    return rc;
}

enum s31_timer_result s31_binding_setfn(s31_timer_binding *b, const void *key,
                                        s31_timer_fn fn, void *arg)
{
    struct entry *e;
    enum s31_timer_result rc;
    if (!b || !key || !fn)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&b->lock))
        return S31_TIMER_SYSTEM;
    if (b->closed)
        return finish(b, S31_TIMER_CLOSED);
    if (lookup(b, key))
        return finish(b, S31_TIMER_BUSY);
    e = lookup(b, NULL);
    if (!e)
        return finish(b, S31_TIMER_FULL);
    rc = s31_timer_create(b->service, fn, arg, &e->id);
    if (rc == S31_TIMER_OK)
        e->key = key;
    return finish(b, rc);
}

enum operation { ARM, DISARM, DONE };
static enum s31_timer_result operate(s31_timer_binding *b, const void *key,
                                    enum operation op, uint32_t us, bool repeat)
{
    struct entry *e;
    enum s31_timer_result rc;
    if (!b || !key || (op == ARM && !us))
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&b->lock))
        return S31_TIMER_SYSTEM;
    if (b->closed)
        return finish(b, S31_TIMER_CLOSED);
    e = lookup(b, key);
    if (!e)
        return finish(b, S31_TIMER_INVALID);
    switch (op) {
    case ARM:
        if (e->retiring)
            return finish(b, S31_TIMER_BUSY);
        rc = s31_timer_arm_us(b->service, e->id, us, repeat);
        break;
    case DISARM:
        rc = s31_timer_disarm(b->service, e->id);
        break;
    case DONE:
        e->retiring = true;
        rc = s31_timer_disarm(b->service, e->id);
        if (rc == S31_TIMER_IN_FLIGHT) {
            rc = S31_TIMER_BUSY;
        } else if (rc == S31_TIMER_OK) {
            rc = s31_timer_delete(b->service, e->id);
            if (rc == S31_TIMER_OK)
                *e = (struct entry){0};
        }
        break;
    default:
        rc = S31_TIMER_INVALID;
    }
    return finish(b, rc);
}

enum s31_timer_result s31_binding_arm(s31_timer_binding *b, const void *key,
                                      uint32_t us, bool repeat)
{
    return operate(b, key, ARM, us, repeat);
}
enum s31_timer_result s31_binding_disarm(s31_timer_binding *b, const void *key)
{
    return operate(b, key, DISARM, 0, false);
}
enum s31_timer_result s31_binding_done(s31_timer_binding *b, const void *key)
{
    return operate(b, key, DONE, 0, false);
}
enum s31_timer_result s31_binding_close(s31_timer_binding *b)
{
    if (!b)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&b->lock))
        return S31_TIMER_SYSTEM;
    b->closed = true;
    return finish(b, s31_timer_service_close(b->service));
}
enum s31_timer_result s31_binding_destroy(s31_timer_binding **owner)
{
    s31_timer_binding *b;
    enum s31_timer_result rc;
    bool closed;
    if (!owner || !*owner)
        return S31_TIMER_INVALID;
    b = *owner;
    if (pthread_mutex_lock(&b->lock))
        return S31_TIMER_SYSTEM;
    closed = b->closed;
    pthread_mutex_unlock(&b->lock);
    if (!closed)
        return S31_TIMER_BUSY;
    /* External callers drained; callbacks may still finish. Never hold
     * the binding lock across the service join. */
    rc = s31_timer_service_destroy(&b->service);
    if (b->service)
        return rc;
    *owner = NULL;
    if (pthread_mutex_destroy(&b->lock))
        return S31_TIMER_SYSTEM;
    free(b);
    return rc;
}
enum s31_timer_result s31_binding_now_us(int64_t *out)
{
    uint64_t value;
    enum s31_timer_result rc;
    if (!out)
        return S31_TIMER_INVALID;
    rc = s31_timer_now_us(&value);
    if (rc != S31_TIMER_OK)
        return rc;
    if (value > INT64_MAX)
        return S31_TIMER_SYSTEM;
    *out = (int64_t)value;
    return S31_TIMER_OK;
}
