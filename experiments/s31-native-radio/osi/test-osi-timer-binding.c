/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "osi-timer-binding.h"
#include "osi-sem.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define OK(x) assert((x) == S31_TIMER_OK)
#define SOK(x) assert((x) == S31_SEM_OK)
#ifdef S31_TEST_LARGE_STACK
#define STACK (1024U * 1024U)
#else
#define STACK (64U * 1024U)
#endif

static s31_sem *event(void)
{
    s31_sem *s;
    SOK(s31_sem_create(8, 0, 1000, &s));
    return s;
}
static void release_event(s31_sem *s)
{
    SOK(s31_sem_close(s));
    SOK(s31_sem_destroy(s));
}
static void wait_event(s31_sem *s)
{
    SOK(s31_sem_take(s, 3000));
}
struct fixture {
    s31_timer_binding *b;
    unsigned char key[20];
    s31_sem *entered, *release, *finished;
    atomic_uint calls;
    bool block;
};
static void cb(void *arg)
{
    struct fixture *f = arg;
    atomic_fetch_add(&f->calls, 1);
    SOK(s31_sem_give(f->entered));
    if (f->block)
        wait_event(f->release);
    SOK(s31_sem_give(f->finished));
}
static void init_fixture(struct fixture *f, s31_timer_binding *b, bool block)
{
    f->b = b;
    memset(f->key, 0xa5, sizeof(f->key));
    f->entered = event();
    f->release = event();
    f->finished = event();
    atomic_init(&f->calls, 0);
    f->block = block;
}
static void dispose_fixture(struct fixture *f)
{
    for (size_t i = 0; i < sizeof(f->key); ++i)
        assert(f->key[i] == 0xa5);
    release_event(f->entered);
    release_event(f->release);
    release_event(f->finished);
}
static void done_after(s31_timer_binding *b, const void *key)
{
    int64_t end, now;
    OK(s31_binding_now_us(&end));
    end += 3000000;
    for (;;) {
        enum s31_timer_result rc = s31_binding_done(b, key);
        if (rc == S31_TIMER_OK)
            return;
        assert(rc == S31_TIMER_BUSY);
        OK(s31_binding_now_us(&now));
        assert(now < end);
        const struct timespec delay = { .tv_nsec = 1000000 };
        nanosleep(&delay, NULL);
    }
}
static void binding_test(void)
{
    s31_timer_binding *b = NULL;
    struct fixture a, z;
    int64_t before, after;
    assert(s31_binding_create(0, STACK, &b) == S31_TIMER_INVALID && !b);
    OK(s31_binding_create(1, STACK, &b));
    init_fixture(&a, b, true);
    init_fixture(&z, b, false);
    assert(s31_binding_destroy(&b) == S31_TIMER_BUSY && b);
    assert(s31_binding_setfn(b, NULL, cb, &a) == S31_TIMER_INVALID);
    assert(s31_binding_setfn(b, a.key, NULL, &a) == S31_TIMER_INVALID);
    assert(s31_binding_arm(b, a.key, 1000, false) == S31_TIMER_INVALID);
    assert(s31_binding_done(b, a.key) == S31_TIMER_INVALID);
    OK(s31_binding_setfn(b, a.key, cb, &a));
    assert(s31_binding_setfn(b, a.key, cb, &z) == S31_TIMER_BUSY);
    assert(s31_binding_setfn(b, z.key, cb, &z) == S31_TIMER_FULL);
    assert(s31_binding_arm(b, a.key, 0, false) == S31_TIMER_INVALID);
    OK(s31_binding_now_us(&before));
    OK(s31_binding_arm(b, a.key, 20000, true));
    wait_event(a.entered);
    OK(s31_binding_now_us(&after));
    assert(after >= before + 20000);
    assert(s31_binding_done(b, a.key) == S31_TIMER_BUSY);
    assert(s31_binding_arm(b, a.key, 1000, true) == S31_TIMER_BUSY);
    assert(s31_binding_setfn(b, a.key, cb, &z) == S31_TIMER_BUSY);
    SOK(s31_sem_give(a.release));
    wait_event(a.finished);
    assert(s31_sem_take(a.entered, 100) == S31_SEM_TIMEOUT);
    assert(atomic_load(&a.calls) == 1);
    done_after(b, a.key);
    assert(s31_binding_arm(b, a.key, 1000, false) == S31_TIMER_INVALID);
    OK(s31_binding_setfn(b, a.key, cb, &z));
    OK(s31_binding_arm(b, a.key, 1000, false));
    wait_event(z.finished);
    assert(atomic_load(&a.calls) == 1 && atomic_load(&z.calls) == 1);
    done_after(b, a.key);
    OK(s31_binding_close(b));
    OK(s31_binding_close(b));
    assert(s31_binding_setfn(b, z.key, cb, &z) == S31_TIMER_CLOSED);
    OK(s31_binding_destroy(&b));
    assert(!b);
    dispose_fixture(&a);
    dispose_fixture(&z);
    puts("BINDING_LIFETIME_PASS");
}
static void close_test(void)
{
    s31_timer_binding *b = NULL;
    struct fixture a, z;
    OK(s31_binding_create(2, STACK, &b));
    init_fixture(&a, b, true);
    init_fixture(&z, b, false);
    OK(s31_binding_setfn(b, a.key, cb, &a));
    OK(s31_binding_setfn(b, z.key, cb, &z));
    OK(s31_binding_arm(b, a.key, 1000, true));
    wait_event(a.entered);
    OK(s31_binding_arm(b, z.key, 1000, false));
    assert(s31_sem_take(z.entered, 50) == S31_SEM_TIMEOUT);
    OK(s31_binding_close(b));
    assert(s31_binding_disarm(b, a.key) == S31_TIMER_CLOSED);
    SOK(s31_sem_give(a.release));
    OK(s31_binding_destroy(&b));
    assert(!b);
    assert(atomic_load(&a.calls) == 1 && !atomic_load(&z.calls));
    dispose_fixture(&a);
    dispose_fixture(&z);
    puts("BINDING_CLOSE_PASS");
}
/* Keep the dispatcher occupied while disarming another pending slot.
 * A subsequent callback establishes that the dispatcher progressed after
 * the cancelled deadline; the test does not rely on racing the timeout. */
static void disarm_test(void)
{
    s31_timer_binding *b = NULL;
    struct fixture a, z;
    int64_t start, now;
    OK(s31_binding_create(2, STACK, &b));
    init_fixture(&a, b, true);
    init_fixture(&z, b, false);
    OK(s31_binding_setfn(b, a.key, cb, &a));
    OK(s31_binding_setfn(b, z.key, cb, &z));
    OK(s31_binding_arm(b, a.key, 1000, false));
    wait_event(a.entered);
    OK(s31_binding_arm(b, z.key, 1000, false));
    OK(s31_binding_disarm(b, z.key));
    OK(s31_binding_now_us(&start));
    do {
        const struct timespec delay = { .tv_nsec = 1000000 };
        nanosleep(&delay, NULL);
        OK(s31_binding_now_us(&now));
    } while (now - start < 10000);
    SOK(s31_sem_give(a.release));
    wait_event(a.finished);
    done_after(b, a.key);
    a.block = false;
    OK(s31_binding_setfn(b, a.key, cb, &a));
    OK(s31_binding_arm(b, a.key, 1000, false));
    wait_event(a.finished);
    assert(atomic_load(&a.calls) == 2 && !atomic_load(&z.calls));
    done_after(b, a.key);
    OK(s31_binding_done(b, z.key));
    OK(s31_binding_close(b));
    OK(s31_binding_destroy(&b));
    dispose_fixture(&a);
    dispose_fixture(&z);
    puts("BINDING_DISARM_PASS");
}
static void self_done(void *arg)
{
    struct fixture *f = arg;
    atomic_fetch_add(&f->calls, 1);
    assert(s31_binding_done(f->b, f->key) == S31_TIMER_BUSY);
    assert(s31_binding_arm(f->b, f->key, 1000, true) == S31_TIMER_BUSY);
    SOK(s31_sem_give(f->finished));
}
static void reentry_test(void)
{
    s31_timer_binding *b = NULL;
    struct fixture a;
    OK(s31_binding_create(1, STACK, &b));
    init_fixture(&a, b, false);
    OK(s31_binding_setfn(b, a.key, self_done, &a));
    OK(s31_binding_arm(b, a.key, 1000, true));
    wait_event(a.finished);
    assert(s31_sem_take(a.finished, 100) == S31_SEM_TIMEOUT);
    assert(atomic_load(&a.calls) == 1);
    done_after(b, a.key);
    OK(s31_binding_close(b));
    OK(s31_binding_destroy(&b));
    dispose_fixture(&a);
    puts("BINDING_REENTRY_PASS");
}
int main(void)
{
    alarm(20);
    assert(s31_binding_now_us(NULL) == S31_TIMER_INVALID);
    binding_test();
    close_test();
    disarm_test();
    reentry_test();
    puts("S31_OSI_TIMER_BINDING_PASS");
    return 0;
}
