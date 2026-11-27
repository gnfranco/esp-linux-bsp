/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "osi-timer.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct timer_slot {
    s31_timer_fn fn;
    void *arg;
    struct timespec due;
    uint32_t generation;
    uint32_t period_us;
    bool armed;
    bool running;
    bool repeat;
    bool auto_repeat;
};

struct s31_timer_service {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t worker;
    uint32_t capacity;
    bool closed;
    bool worker_error;
    enum s31_timer_wait_state wait_state;
    struct timer_slot slots[];
};

static int compare(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec)
        return a->tv_sec < b->tv_sec ? -1 : 1;
    return (a->tv_nsec > b->tv_nsec) - (a->tv_nsec < b->tv_nsec);
}

static int make_deadline(uint32_t us, struct timespec *due)
{
    uint64_t ns, sec, add;
    const uint64_t time_max = sizeof(time_t) == 4 ? INT32_MAX : INT64_MAX;
    _Static_assert(sizeof(time_t) == 4 || sizeof(time_t) == 8, "Linux time_t");
    _Static_assert((time_t)-1 < 0, "signed Linux time_t");
    if (clock_gettime(CLOCK_MONOTONIC, due) || due->tv_sec < 0)
        return -1;
    ns = (uint64_t)due->tv_nsec + (uint64_t)(us % 1000000U) * 1000U;
    add = (uint64_t)(us / 1000000U) + ns / 1000000000U;
    if (add > time_max || (uint64_t)due->tv_sec > time_max - add)
        return -1;
    sec = (uint64_t)due->tv_sec + add;
    due->tv_sec = (time_t)sec;
    due->tv_nsec = (long)(ns % 1000000000U);
    return 0;
}

enum s31_timer_result s31_timer_now_us(uint64_t *out)
{
    struct timespec now;
    uint64_t frac;
    if (!out)
        return S31_TIMER_INVALID;
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0)
        return S31_TIMER_SYSTEM;
    frac = (uint64_t)now.tv_nsec / 1000U;
    if ((uint64_t)now.tv_sec > (UINT64_MAX - frac) / 1000000U)
        return S31_TIMER_SYSTEM;
    *out = (uint64_t)now.tv_sec * 1000000U + frac;
    return S31_TIMER_OK;
}

/* Under lock. Connection to the service must remain valid until destroy. */
static struct timer_slot *find_slot(s31_timer_service *service,
                                    struct s31_timer_id id)
{
    struct timer_slot *slot;
    if (id.slot >= service->capacity || !id.generation)
        return NULL;
    slot = &service->slots[id.slot];
    return slot->fn && slot->generation == id.generation ? slot : NULL;
}

static void close_locked(s31_timer_service *service)
{
    service->closed = true;
    for (uint32_t i = 0; i < service->capacity; ++i) {
        service->slots[i].armed = false;
        service->slots[i].auto_repeat = false;
    }
}

static struct timer_slot *earliest(s31_timer_service *service)
{
    struct timer_slot *next = NULL;
    for (uint32_t i = 0; i < service->capacity; ++i) {
        struct timer_slot *slot = &service->slots[i];
        if (slot->armed && (!next || compare(&slot->due, &next->due) < 0))
            next = slot;
    }
    return next;
}

static void *dispatch(void *arg)
{
    s31_timer_service *service = arg;
    if (pthread_mutex_lock(&service->lock))
        return NULL; /* impossible for a correctly initialized private mutex */
    while (!service->closed) {
        struct timer_slot *slot = earliest(service);
        struct timespec now;
        int rc;
        if (!slot) {
            service->wait_state = S31_TIMER_IDLE_WAIT;
            rc = pthread_cond_wait(&service->changed, &service->lock);
            service->wait_state = S31_TIMER_ACTIVE;
            if (rc)
                goto failed;
            continue;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now))
            goto failed;
        if (compare(&now, &slot->due) < 0) {
            /* Copy the deadline: another caller can update the slot. */
            struct timespec until = slot->due;
            service->wait_state = S31_TIMER_DEADLINE_WAIT;
            rc = pthread_cond_timedwait(&service->changed, &service->lock, &until);
            service->wait_state = S31_TIMER_ACTIVE;
            if (rc && rc != ETIMEDOUT)
                goto failed;
            continue;
        }
        slot->armed = false;
        slot->running = true;
        slot->auto_repeat = slot->repeat;
        s31_timer_fn fn = slot->fn;
        void *fn_arg = slot->arg;
        pthread_mutex_unlock(&service->lock);
        fn(fn_arg);
        pthread_mutex_lock(&service->lock);
        slot->running = false;
        /*
         * arm/disarm/close during the callback clear auto_repeat. Delete
         * cannot recycle an in-flight slot. Never override a new schedule.
         */
        if (!service->closed && slot->auto_repeat) {
            if (make_deadline(slot->period_us, &slot->due))
                goto failed;
            slot->armed = true;
        }
        slot->auto_repeat = false;
    }
    service->wait_state = S31_TIMER_STOPPED;
    pthread_mutex_unlock(&service->lock);
    return NULL;
failed:
    service->worker_error = true;
    service->wait_state = S31_TIMER_STOPPED;
    close_locked(service);
    pthread_mutex_unlock(&service->lock);
    return NULL;
}

enum s31_timer_result s31_timer_service_create(uint32_t capacity,
                                               size_t stack_size,
                                               s31_timer_service **out)
{
    s31_timer_service *service;
    pthread_condattr_t attr;
    pthread_attr_t worker_attr;
    int rc;
    if (!out)
        return S31_TIMER_INVALID;
    *out = NULL;
    if (!capacity || capacity > S31_TIMER_MAX_SLOTS || !stack_size)
        return S31_TIMER_INVALID;
    service = calloc(1, sizeof(*service) + capacity * sizeof(service->slots[0]));
    if (!service)
        return S31_TIMER_SYSTEM;
    service->capacity = capacity;
    if (pthread_mutex_init(&service->lock, NULL))
        goto free_service;
    if (pthread_condattr_init(&attr))
        goto destroy_mutex;
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!rc)
        rc = pthread_cond_init(&service->changed, &attr);
    pthread_condattr_destroy(&attr);
    if (rc)
        goto destroy_mutex;
    if (pthread_attr_init(&worker_attr))
        goto destroy_cond;
    rc = pthread_attr_setstacksize(&worker_attr, stack_size);
    if (!rc)
        rc = pthread_create(&service->worker, &worker_attr, dispatch, service);
    pthread_attr_destroy(&worker_attr);
    if (rc)
        goto destroy_cond;
    *out = service;
    return S31_TIMER_OK;
destroy_cond:
    pthread_cond_destroy(&service->changed);
destroy_mutex:
    pthread_mutex_destroy(&service->lock);
free_service:
    free(service);
    return S31_TIMER_SYSTEM;
}

enum s31_timer_result s31_timer_create(s31_timer_service *service,
                                      s31_timer_fn fn, void *arg,
                                      struct s31_timer_id *out)
{
    enum s31_timer_result result = S31_TIMER_FULL;
    if (!service || !fn || !out)
        return S31_TIMER_INVALID;
    *out = (struct s31_timer_id){0};
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    if (service->closed)
        result = S31_TIMER_CLOSED;
    else for (uint32_t i = 0; i < service->capacity; ++i) {
        struct timer_slot *slot = &service->slots[i];
        if (!slot->fn && slot->generation != UINT32_MAX) {
            uint32_t generation = slot->generation + 1;
            *slot = (struct timer_slot){ .fn = fn, .arg = arg,
                                         .generation = generation };
            *out = (struct s31_timer_id){ .slot = i, .generation = generation };
            result = S31_TIMER_OK;
            break;
        }
    }
    pthread_mutex_unlock(&service->lock);
    return result;
}

enum s31_timer_result s31_timer_arm_us(s31_timer_service *service,
                                      struct s31_timer_id id,
                                      uint32_t delay_us, bool repeat)
{
    struct timer_slot *slot;
    struct timespec due;
    enum s31_timer_result result;
    if (!service || !delay_us)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    slot = find_slot(service, id);
    if (service->closed)
        result = S31_TIMER_CLOSED;
    else if (!slot)
        result = S31_TIMER_INVALID;
    else if (make_deadline(delay_us, &due))
        result = S31_TIMER_SYSTEM;
    else {
        slot->due = due;
        slot->period_us = delay_us;
        slot->repeat = repeat;
        slot->armed = true;
        slot->auto_repeat = false;
        result = pthread_cond_signal(&service->changed) ? S31_TIMER_SYSTEM : S31_TIMER_OK;
    }
    pthread_mutex_unlock(&service->lock);
    return result;
}

enum s31_timer_result s31_timer_disarm(s31_timer_service *service,
                                      struct s31_timer_id id)
{
    struct timer_slot *slot;
    enum s31_timer_result result;
    if (!service)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    slot = find_slot(service, id);
    if (service->closed)
        result = S31_TIMER_CLOSED;
    else if (!slot)
        result = S31_TIMER_INVALID;
    else {
        slot->armed = false;
        slot->auto_repeat = false;
        if (pthread_cond_signal(&service->changed))
            result = S31_TIMER_SYSTEM;
        else
            result = slot->running ? S31_TIMER_IN_FLIGHT : S31_TIMER_OK;
    }
    pthread_mutex_unlock(&service->lock);
    return result;
}

enum s31_timer_result s31_timer_delete(s31_timer_service *service,
                                      struct s31_timer_id id)
{
    struct timer_slot *slot;
    enum s31_timer_result result;
    if (!service)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    slot = find_slot(service, id);
    if (service->closed)
        result = S31_TIMER_CLOSED;
    else if (!slot)
        result = S31_TIMER_INVALID;
    else if (slot->running)
        result = S31_TIMER_BUSY;
    else {
        uint32_t generation = slot->generation;
        *slot = (struct timer_slot){ .generation = generation };
        result = pthread_cond_signal(&service->changed) ? S31_TIMER_SYSTEM : S31_TIMER_OK;
    }
    pthread_mutex_unlock(&service->lock);
    return result;
}

enum s31_timer_result s31_timer_service_close(s31_timer_service *service)
{
    int rc;
    if (!service)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    close_locked(service);
    rc = pthread_cond_signal(&service->changed);
    pthread_mutex_unlock(&service->lock);
    return rc ? S31_TIMER_SYSTEM : S31_TIMER_OK;
}

enum s31_timer_result s31_timer_service_snapshot(s31_timer_service *service,
                                                 struct s31_timer_snapshot *out)
{
    if (!service || !out)
        return S31_TIMER_INVALID;
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    *out = (struct s31_timer_snapshot) {
        .wait_state = service->wait_state,
        .closed = service->closed,
        .worker_failed = service->worker_error,
    };
    pthread_mutex_unlock(&service->lock);
    return S31_TIMER_OK;
}

enum s31_timer_result s31_timer_service_destroy(s31_timer_service **owner)
{
    s31_timer_service *service;
    bool failed;
    int cond_rc, mutex_rc;
    if (!owner || !*owner)
        return S31_TIMER_INVALID;
    service = *owner;
    if (pthread_equal(pthread_self(), service->worker))
        return S31_TIMER_BUSY;
    if (pthread_mutex_lock(&service->lock))
        return S31_TIMER_SYSTEM;
    if (!service->closed) {
        pthread_mutex_unlock(&service->lock);
        return S31_TIMER_BUSY;
    }
    pthread_mutex_unlock(&service->lock);
    /* No external callers remain; join the callback/worker before freeing. */
    if (pthread_join(service->worker, NULL))
        return S31_TIMER_SYSTEM;
    failed = service->worker_error;
    *owner = NULL; /* terminal ownership transfer, even on teardown error */
    cond_rc = pthread_cond_destroy(&service->changed);
    mutex_rc = pthread_mutex_destroy(&service->lock);
    if (cond_rc || mutex_rc)
        return S31_TIMER_SYSTEM; /* invariant failure: do not reuse/free */
    free(service);
    return failed ? S31_TIMER_WORKER_FAILED : S31_TIMER_OK;
}
