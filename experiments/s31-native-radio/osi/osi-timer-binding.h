/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_OSI_TIMER_BINDING_H
#define S31_OSI_TIMER_BINDING_H
#include "osi-timer.h"

typedef struct s31_timer_binding s31_timer_binding;

/*
 * Experimental typed, thread-context mapping, NOT an ETSTimer ABI.
 * A key is the address of a live caller-owned object; never dereferenced.
 * A key and its callback argument must remain valid until done succeeds
 * or the service is joined. Close does not wait for a selected callback.
 * Duplicate bind is BUSY (explicit done before replacement). Calls are
 * serialized. done cancels recurrence; BUSY means a callback can still run,
 * so retry after callback completion. Once done starts, arm is refused.
 * Drain all old key users before reusing an address; keys have no external
 * generation. On SYSTEM, stop use and close/drain/destroy rather than retry.
 * A successful join consumes *owner even on terminal teardown errors;
 * a failed join or BUSY preserves ownership.
 * No IRQ context, object layout access, or void vendor callback wrappers.
 * The owner must close, drain external borrowers, then destroy. Callbacks
 * may use the binding but may not destroy it or wait for another callback.
 */
enum s31_timer_result s31_binding_create(uint32_t capacity, size_t stack_size,
                                        s31_timer_binding **out);
enum s31_timer_result s31_binding_setfn(s31_timer_binding *b, const void *key,
                                        s31_timer_fn fn, void *arg);
enum s31_timer_result s31_binding_arm(s31_timer_binding *b, const void *key,
                                      uint32_t us, bool repeat);
enum s31_timer_result s31_binding_disarm(s31_timer_binding *b, const void *key);
enum s31_timer_result s31_binding_done(s31_timer_binding *b, const void *key);
enum s31_timer_result s31_binding_close(s31_timer_binding *b);
enum s31_timer_result s31_binding_destroy(s31_timer_binding **owner);
enum s31_timer_result s31_binding_now_us(int64_t *out);
#endif
