/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_OSI_SEM_H
#define S31_OSI_SEM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct s31_sem s31_sem;

enum s31_sem_result {
    S31_SEM_OK,
    S31_SEM_INVALID,
    S31_SEM_FULL,
    S31_SEM_WOULD_BLOCK,
    S31_SEM_TIMEOUT,
    S31_SEM_CLOSED,
    S31_SEM_BUSY,
    S31_SEM_SYSTEM,
};

#define S31_SEM_FOREVER UINT32_MAX

struct s31_sem_snapshot {
    uint32_t count;
    uint32_t capacity;
    uint32_t waiters;
    bool closed;
};

/*
 * Thread context ONLY. No IRQ/ISR, hardware/DMA memory or FreeRTOS ABI
 * compatibility is implied. The owner selects the tick rate explicitly.
 * Zero ticks is nonblocking; UINT32_MAX waits until give or close.
 * A successful give makes one token available, with no overflow.
 *
 * Close rejects new operations and wakes pending takes. It is idempotent.
 * The owner must stop publishing the pointer, close, join/drain every
 * caller, and only then destroy. No caller may race destruction, including
 * one that has obtained a pointer but not yet entered the API.
 * Deferred pthread cancellation of a blocked take is supported; asynchronous
 * cancellation and callbacks from signal handlers are unsupported.
 */
enum s31_sem_result s31_sem_create(uint32_t capacity, uint32_t initial,
                                   uint32_t tick_hz, s31_sem **out);
enum s31_sem_result s31_sem_take(s31_sem *sem, uint32_t ticks);
enum s31_sem_result s31_sem_give(s31_sem *sem);
enum s31_sem_result s31_sem_close(s31_sem *sem);
enum s31_sem_result s31_sem_snapshot(s31_sem *sem,
                                     struct s31_sem_snapshot *out);
enum s31_sem_result s31_sem_destroy(s31_sem *sem);

#endif
