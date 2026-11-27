/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "osi-sem.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct s31_sem {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    uint32_t count;
    uint32_t capacity;
    uint32_t tick_hz;
    uint32_t waiters;
    bool closed;
};

/*
 * Absolute monotonic deadline, rounded up to nanoseconds. Calculate once,
 * before acquiring the mutex, so spurious wakeups never extend the timeout.
 * Finite tick values are strictly below UINT32_MAX. The supported Linux
 * time_t is signed; check conversion and overflow (including time32).
 */
static int deadline(uint32_t ticks, uint32_t hz, struct timespec *until)
{
    uint64_t sec, ns;
    if (clock_gettime(CLOCK_MONOTONIC, until) || until->tv_sec < 0)
        return -1;
    sec = ticks / hz;
    ns = (((uint64_t)(ticks % hz) * UINT64_C(1000000000)) + hz - 1) / hz;
    ns += (uint64_t)until->tv_nsec;
    sec += ns / UINT64_C(1000000000);
    if ((uint64_t)until->tv_sec > (uint64_t)INT64_MAX - sec)
        return -1;
    sec += (uint64_t)until->tv_sec;
    until->tv_sec = (time_t)sec;
    if (until->tv_sec < 0 || (uint64_t)until->tv_sec != sec)
        return -1;
    until->tv_nsec = (long)(ns % UINT64_C(1000000000));
    return 0;
}

enum s31_sem_result s31_sem_create(uint32_t capacity, uint32_t initial,
                                   uint32_t tick_hz, s31_sem **out)
{
    s31_sem *sem;
    pthread_condattr_t attr;
    int rc;

    if (!out)
        return S31_SEM_INVALID;
    *out = NULL;
    if (!capacity || initial > capacity || !tick_hz ||
        tick_hz > UINT32_C(1000000000))
        return S31_SEM_INVALID;
    sem = calloc(1, sizeof(*sem));
    if (!sem)
        return S31_SEM_SYSTEM;
    sem->count = initial;
    sem->capacity = capacity;
    sem->tick_hz = tick_hz;
    if (pthread_mutex_init(&sem->lock, NULL))
        goto free_sem;
    if (pthread_condattr_init(&attr))
        goto destroy_mutex;
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!rc)
        rc = pthread_cond_init(&sem->changed, &attr);
    pthread_condattr_destroy(&attr);
    if (rc)
        goto destroy_mutex;
    *out = sem;
    return S31_SEM_OK;

destroy_mutex:
    pthread_mutex_destroy(&sem->lock);
free_sem:
    free(sem);
    return S31_SEM_SYSTEM;
}

/* POSIX cond_wait reacquires the mutex before deferred cancellation cleanup. */
static void finish_wait(void *arg)
{
    s31_sem *sem = arg;
    --sem->waiters;
    pthread_mutex_unlock(&sem->lock);
}

/* Caller holds the lock, registered this waiter and installed cleanup. */
static enum s31_sem_result wait_locked(s31_sem *sem, uint32_t ticks,
                                       const struct timespec *until)
{
    int rc = 0;

    while (!sem->closed && !sem->count && !rc)
        rc = ticks == S31_SEM_FOREVER
            ? pthread_cond_wait(&sem->changed, &sem->lock)
            : pthread_cond_timedwait(&sem->changed, &sem->lock, until);
    /* Close wins; an observed token may win a give/timeout boundary race. */
    if (sem->closed)
        return S31_SEM_CLOSED;
    if (rc && rc != ETIMEDOUT)
        return S31_SEM_SYSTEM;
    if (sem->count) {
        --sem->count;
        return S31_SEM_OK;
    }
    return rc == ETIMEDOUT ? S31_SEM_TIMEOUT : S31_SEM_SYSTEM;
}

enum s31_sem_result s31_sem_take(s31_sem *sem, uint32_t ticks)
{
    struct timespec until;
    bool deadline_error;
    /* pthread_cleanup_push may use setjmp; keep the returned value valid. */
    volatile enum s31_sem_result result;

    if (!sem)
        return S31_SEM_INVALID;
    deadline_error = ticks && ticks != S31_SEM_FOREVER &&
                     deadline(ticks, sem->tick_hz, &until);
    if (pthread_mutex_lock(&sem->lock))
        return S31_SEM_SYSTEM;

    if (sem->closed)
        result = S31_SEM_CLOSED;
    else if (sem->count) {
        --sem->count;
        result = S31_SEM_OK;
    } else if (!ticks)
        result = S31_SEM_WOULD_BLOCK;
    else if (deadline_error || sem->waiters == UINT32_MAX)
        result = S31_SEM_SYSTEM;
    else {
        ++sem->waiters;
        /* No cancellation point between registering and installing cleanup. */
        pthread_cleanup_push(finish_wait, sem);
        result = wait_locked(sem, ticks, &until);
        pthread_cleanup_pop(1);
        return result;
    }
    pthread_mutex_unlock(&sem->lock);
    return result;
}

enum s31_sem_result s31_sem_give(s31_sem *sem)
{
    enum s31_sem_result result;
    if (!sem)
        return S31_SEM_INVALID;
    if (pthread_mutex_lock(&sem->lock))
        return S31_SEM_SYSTEM;
    if (sem->closed)
        result = S31_SEM_CLOSED;
    else if (sem->count == sem->capacity)
        result = S31_SEM_FULL;
    else {
        ++sem->count;
        result = pthread_cond_signal(&sem->changed) ? S31_SEM_SYSTEM : S31_SEM_OK;
    }
    pthread_mutex_unlock(&sem->lock);
    return result;
}

enum s31_sem_result s31_sem_close(s31_sem *sem)
{
    int rc;
    if (!sem)
        return S31_SEM_INVALID;
    if (pthread_mutex_lock(&sem->lock))
        return S31_SEM_SYSTEM;
    sem->closed = true;
    rc = pthread_cond_broadcast(&sem->changed);
    pthread_mutex_unlock(&sem->lock);
    return rc ? S31_SEM_SYSTEM : S31_SEM_OK;
}

enum s31_sem_result s31_sem_snapshot(s31_sem *sem,
                                     struct s31_sem_snapshot *out)
{
    if (!sem || !out)
        return S31_SEM_INVALID;
    if (pthread_mutex_lock(&sem->lock))
        return S31_SEM_SYSTEM;
    *out = (struct s31_sem_snapshot) {
        .count = sem->count,
        .capacity = sem->capacity,
        .waiters = sem->waiters,
        .closed = sem->closed,
    };
    pthread_mutex_unlock(&sem->lock);
    return S31_SEM_OK;
}

enum s31_sem_result s31_sem_destroy(s31_sem *sem)
{
    if (!sem)
        return S31_SEM_INVALID;
    /* The owner has already excluded ALL concurrent users. */
    if (pthread_mutex_lock(&sem->lock))
        return S31_SEM_SYSTEM;
    if (!sem->closed || sem->waiters) {
        pthread_mutex_unlock(&sem->lock);
        return S31_SEM_BUSY;
    }
    pthread_mutex_unlock(&sem->lock);
    if (pthread_cond_destroy(&sem->changed))
        return S31_SEM_SYSTEM;
    if (pthread_mutex_destroy(&sem->lock))
        return S31_SEM_SYSTEM; /* broken owner contract; do not reuse */
    free(sem);
    return S31_SEM_OK;
}
