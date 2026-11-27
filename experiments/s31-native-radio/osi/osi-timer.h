/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_OSI_TIMER_H
#define S31_OSI_TIMER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct s31_timer_service s31_timer_service;
typedef void (*s31_timer_fn)(void *);

struct s31_timer_id {
    uint32_t slot;
    uint32_t generation;
};

enum s31_timer_result {
    S31_TIMER_OK,
    S31_TIMER_INVALID,
    S31_TIMER_FULL,
    S31_TIMER_CLOSED,
    S31_TIMER_IN_FLIGHT,
    S31_TIMER_BUSY,
    S31_TIMER_SYSTEM,
    S31_TIMER_WORKER_FAILED,
};

#define S31_TIMER_MAX_SLOTS 16U

enum s31_timer_wait_state {
    S31_TIMER_ACTIVE,
    S31_TIMER_IDLE_WAIT,
    S31_TIMER_DEADLINE_WAIT,
    S31_TIMER_STOPPED,
};
struct s31_timer_snapshot {
    enum s31_timer_wait_state wait_state;
    bool closed;
    bool worker_failed;
};

/*
 * Experimental thread-context service, not a ROM timer ABI or IRQ facility.
 * One dispatcher, fixed slot limit; callbacks never execute concurrently.
 * Callbacks execute outside the lock and must return in bounded time.
 * Arm accepts positive microseconds, one-shot or fixed-delay periodic
 * (next period begins after callback completion). Rearm replaces schedule.
 * Disarm cancels pending work and periodic rearm; IN_FLIGHT means an already
 * selected callback can still run/finish. Delete returns BUSY if in flight.
 * Generation IDs prevent stale slot reuse within the same service lifetime;
 * IDs are not transferable across services. Exhausted generations retire.
 * Callback arguments stay valid until delete succeeds or dispatcher is joined.
 * Signal failure after a state change does not roll back the state.
 *
 * The owner must stop publishing the service pointer, close, drain all
 * external callers, then destroy (joins dispatcher and frees memory).
 * Destroy consumes *owner (sets NULL) after join; WORKER_FAILED means the
 * worker reported an error but the service was reclaimed. A rare post-join
 * synchronization teardown failure is terminal too (owner is NULL). A join
 * failure leaves owner intact. BUSY leaves owner intact.
 * Callbacks may arm/disarm but must not destroy, exit/cancel dispatcher,
 * or wait for another callback of the same service. IRQ/async-signal use,
 * abrupt thread cancellation, and concurrent destruction are unsupported.
 */
enum s31_timer_result s31_timer_service_create(uint32_t capacity,
                                               size_t stack_size,
                                               s31_timer_service **out);
enum s31_timer_result s31_timer_create(s31_timer_service *service,
                                      s31_timer_fn fn, void *arg,
                                      struct s31_timer_id *out);
enum s31_timer_result s31_timer_arm_us(s31_timer_service *service,
                                      struct s31_timer_id id,
                                      uint32_t delay_us, bool repeat);
enum s31_timer_result s31_timer_disarm(s31_timer_service *service,
                                      struct s31_timer_id id);
enum s31_timer_result s31_timer_delete(s31_timer_service *service,
                                      struct s31_timer_id id);
enum s31_timer_result s31_timer_service_close(s31_timer_service *service);
enum s31_timer_result s31_timer_service_snapshot(s31_timer_service *service,
                                                 struct s31_timer_snapshot *out);
enum s31_timer_result s31_timer_service_destroy(s31_timer_service **owner);
enum s31_timer_result s31_timer_now_us(uint64_t *out);

#endif
