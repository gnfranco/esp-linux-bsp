/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "osi-sem.h"
#include "osi-timer.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) fail(#x); } while (0)
#define OK(x) CHECK((x) == S31_TIMER_OK)
#define SEM_OK(x) CHECK((x) == S31_SEM_OK)
#ifdef S31_TEST_LARGE_STACK
#define STACK_SIZE (1024U * 1024U)
#else
#define STACK_SIZE (64U * 1024U)
#endif

static void report(const char *s)
{
    (void)write(STDOUT_FILENO, s, strlen(s));
    (void)write(STDOUT_FILENO, "\n", 1);
}

static void fail(const char *s)
{
    report("S31_OSI_TIMER_FAIL");
    report(s);
    _Exit(1);
}

static uint64_t now_us(void)
{
    uint64_t t;
    OK(s31_timer_now_us(&t));
    return t;
}

static s31_sem *event_create(void)
{
    s31_sem *ev;
    SEM_OK(s31_sem_create(16, 0, 1000, &ev));
    return ev;
}

static void event_destroy(s31_sem *ev)
{
    SEM_OK(s31_sem_close(ev));
    SEM_OK(s31_sem_destroy(ev));
}

static void await(s31_sem *ev)
{
    SEM_OK(s31_sem_take(ev, 3000));
}

static void no_event(s31_sem *ev, uint32_t ticks)
{
    CHECK(s31_sem_take(ev, ticks) == S31_SEM_TIMEOUT);
}

static void dispose(s31_timer_service *service)
{
    OK(s31_timer_service_close(service));
    OK(s31_timer_service_destroy(&service));
    CHECK(!service);
}

static void pause_us(uint32_t us)
{
    struct timespec delay = { .tv_sec = us / 1000000U,
                             .tv_nsec = (long)(us % 1000000U) * 1000L };
    while (nanosleep(&delay, &delay) && errno == EINTR)
        ;
}

static void wait_state(s31_timer_service *service, enum s31_timer_wait_state wanted)
{
    uint64_t end = now_us() + 3000000U;
    for (;;) {
        struct s31_timer_snapshot snap;
        OK(s31_timer_service_snapshot(service, &snap));
        CHECK(!snap.closed && !snap.worker_failed);
        if (snap.wait_state == wanted)
            return;
        CHECK(now_us() < end);
        pause_us(1000);
    }
}

static void delete_after(s31_timer_service *service, struct s31_timer_id id)
{
    uint64_t end = now_us() + 3000000U;
    struct timespec wait = { .tv_nsec = 1000000L };
    enum s31_timer_result rc;
    do {
        rc = s31_timer_delete(service, id);
        if (rc == S31_TIMER_BUSY) {
            CHECK(now_us() < end);
            (void)nanosleep(&wait, NULL);
        }
    } while (rc == S31_TIMER_BUSY);
    CHECK(rc == S31_TIMER_OK);
}

struct collector {
    s31_sem *event;
    unsigned values[8];
    uint64_t times[8];
    atomic_uint n;
};
struct tagged {
    struct collector *collector;
    unsigned tag;
};

static void collect(void *arg)
{
    struct tagged *t = arg;
    struct collector *c = t->collector;
    unsigned i = atomic_fetch_add(&c->n, 1);
    CHECK(i < 8);
    c->values[i] = t->tag;
    c->times[i] = now_us();
    SEM_OK(s31_sem_give(c->event));
}

static void order_test(void)
{
    s31_timer_service *service = NULL;
    struct collector c = { .event = event_create(), .n = 0 };
    struct tagged a = { &c, 1 }, b = { &c, 2 };
    struct s31_timer_id first, second, stale, extra;
    uint64_t ta, tb;
    CHECK(s31_timer_now_us(NULL) == S31_TIMER_INVALID);
    CHECK(s31_timer_service_create(0, STACK_SIZE, &service) == S31_TIMER_INVALID && !service);
    CHECK(s31_timer_service_create(S31_TIMER_MAX_SLOTS + 1, STACK_SIZE, &service) == S31_TIMER_INVALID && !service);
    CHECK(s31_timer_service_create(1, STACK_SIZE, NULL) == S31_TIMER_INVALID);
    OK(s31_timer_service_create(2, STACK_SIZE, &service));
    CHECK(s31_timer_service_destroy(&service) == S31_TIMER_BUSY && service);
    wait_state(service, S31_TIMER_IDLE_WAIT);
    CHECK(s31_timer_create(service, NULL, NULL, &extra) == S31_TIMER_INVALID);
    OK(s31_timer_create(service, collect, &a, &first));
    OK(s31_timer_create(service, collect, &b, &second));
    CHECK(s31_timer_create(service, collect, &a, &extra) == S31_TIMER_FULL);
    CHECK(s31_timer_arm_us(service, first, 0, false) == S31_TIMER_INVALID);
    ta = now_us();
    OK(s31_timer_arm_us(service, first, 500000, false));
    wait_state(service, S31_TIMER_DEADLINE_WAIT);
    tb = now_us();
    OK(s31_timer_arm_us(service, second, 20000, false));
    await(c.event);
    CHECK(c.values[0] == 2 && c.times[0] >= tb + 20000 &&
          c.times[0] < ta + 350000);
    await(c.event);
    CHECK(c.values[1] == 1 && c.times[1] >= ta + 500000);
    no_event(c.event, 50);
    CHECK(atomic_load(&c.n) == 2);
    delete_after(service, first);
    stale = first;
    OK(s31_timer_create(service, collect, &a, &first));
    CHECK(stale.slot == first.slot && stale.generation != first.generation);
    CHECK(s31_timer_arm_us(service, stale, 1, false) == S31_TIMER_INVALID);
    CHECK(s31_timer_disarm(service, stale) == S31_TIMER_INVALID);
    CHECK(s31_timer_delete(service, stale) == S31_TIMER_INVALID);
    OK(s31_timer_arm_us(service, first, UINT32_MAX, false));
    OK(s31_timer_disarm(service, first));
    OK(s31_timer_arm_us(service, first, 500000, false));
    OK(s31_timer_disarm(service, first));
    no_event(c.event, 550);
    CHECK(atomic_load(&c.n) == 2);
    /* Rearm a pending long deadline to an earlier one. */
    wait_state(service, S31_TIMER_IDLE_WAIT);
    ta = now_us();
    OK(s31_timer_arm_us(service, first, 500000, false));
    wait_state(service, S31_TIMER_DEADLINE_WAIT);
    tb = now_us();
    OK(s31_timer_arm_us(service, first, 20000, false));
    await(c.event);
    CHECK(c.values[2] == 1 && c.times[2] >= tb + 20000 &&
          c.times[2] < ta + 350000);
    no_event(c.event, 550);
    CHECK(atomic_load(&c.n) == 3);
    delete_after(service, first);
    delete_after(service, second);
    dispose(service);
    event_destroy(c.event);
    report("TIMER_ORDER_REARM_STALE_PASS");
}

struct periodic {
    s31_timer_service *service;
    struct s31_timer_id id;
    s31_sem *event;
    atomic_uint n;
    uint64_t times[3], before_return[3];
};
static void periodic_cb(void *arg)
{
    struct periodic *p = arg;
    unsigned i = atomic_fetch_add(&p->n, 1);
    CHECK(i < 3);
    p->times[i] = now_us();
    if (i == 0)
        pause_us(65000); /* callback spans multiple periods */
    if (i == 2)
        CHECK(s31_timer_disarm(p->service, p->id) == S31_TIMER_IN_FLIGHT);
    p->before_return[i] = now_us();
    SEM_OK(s31_sem_give(p->event));
}
static void periodic_test(void)
{
    struct periodic p = { .event = event_create(), .n = 0 };
    OK(s31_timer_service_create(1, STACK_SIZE, &p.service));
    OK(s31_timer_create(p.service, periodic_cb, &p, &p.id));
    OK(s31_timer_arm_us(p.service, p.id, 20000, true));
    await(p.event); await(p.event); await(p.event);
    CHECK(p.before_return[0] >= p.times[0] + 65000);
    CHECK(p.times[1] >= p.before_return[0] + 20000);
    CHECK(p.times[2] >= p.before_return[1] + 20000);
    no_event(p.event, 100);
    CHECK(atomic_load(&p.n) == 3);
    delete_after(p.service, p.id);
    dispose(p.service);
    event_destroy(p.event);
    report("TIMER_PERIODIC_FIXED_DELAY_PASS");
}

struct blocked {
    s31_sem *started, *release, *done;
    atomic_uint n;
};
static struct blocked blocked_create(void)
{
    return (struct blocked){ .started = event_create(),
        .release = event_create(), .done = event_create(), .n = 0 };
}
static void blocked_dispose(struct blocked *b)
{
    event_destroy(b->started);
    event_destroy(b->release);
    event_destroy(b->done);
}
static void blocked_cb(void *arg)
{
    struct blocked *b = arg;
    unsigned n = atomic_fetch_add(&b->n, 1);
    if (n == 0) {
        SEM_OK(s31_sem_give(b->started));
        await(b->release);
    }
    SEM_OK(s31_sem_give(b->done));
}

static void inflight_test(void)
{
    s31_timer_service *service;
    struct s31_timer_id id;
    struct blocked b = blocked_create();
    OK(s31_timer_service_create(1, STACK_SIZE, &service));
    OK(s31_timer_create(service, blocked_cb, &b, &id));
    OK(s31_timer_arm_us(service, id, 1000, true));
    await(b.started);
    CHECK(s31_timer_disarm(service, id) == S31_TIMER_IN_FLIGHT);
    CHECK(s31_timer_delete(service, id) == S31_TIMER_BUSY);
    SEM_OK(s31_sem_give(b.release));
    await(b.done);
    no_event(b.done, 100);
    CHECK(atomic_load(&b.n) == 1);
    delete_after(service, id);
    blocked_dispose(&b);

    /* Rearm without preceding disarm; new deadline is due during callback. */
    b = blocked_create();
    OK(s31_timer_create(service, blocked_cb, &b, &id));
    OK(s31_timer_arm_us(service, id, 1000, true));
    await(b.started);
    OK(s31_timer_arm_us(service, id, 300000, false));
    pause_us(350000);
    SEM_OK(s31_sem_give(b.release));
    await(b.done);
    SEM_OK(s31_sem_take(b.done, 200)); /* not another 300 ms from completion */
    no_event(b.done, 100);
    CHECK(atomic_load(&b.n) == 2);
    delete_after(service, id);
    dispose(service);
    blocked_dispose(&b);
    report("TIMER_IN_FLIGHT_REARM_PASS");
}

struct destroy_arg {
    s31_timer_service *service;
    s31_sem *entered, *joined;
    enum s31_timer_result result;
};
static void *destroy_main(void *arg)
{
    struct destroy_arg *a = arg;
    SEM_OK(s31_sem_give(a->entered));
    a->result = s31_timer_service_destroy(&a->service);
    SEM_OK(s31_sem_give(a->joined));
    return NULL;
}

static void close_test(void)
{
    s31_timer_service *service;
    struct s31_timer_id id, pending, extra;
    struct blocked b = blocked_create(), other = blocked_create();
    struct destroy_arg a = { .entered = event_create(), .joined = event_create() };
    pthread_t thread;
    pthread_attr_t attr;
    OK(s31_timer_service_create(2, STACK_SIZE, &service));
    OK(s31_timer_create(service, blocked_cb, &b, &id));
    OK(s31_timer_create(service, blocked_cb, &other, &pending));
    OK(s31_timer_arm_us(service, id, 1000, true));
    await(b.started);
    OK(s31_timer_arm_us(service, pending, 1000, false));
    no_event(other.started, 50); /* dispatcher cannot run both callbacks */
    OK(s31_timer_service_close(service));
    OK(s31_timer_service_close(service));
    CHECK(s31_timer_arm_us(service, id, 1000, false) == S31_TIMER_CLOSED);
    CHECK(s31_timer_create(service, blocked_cb, &b, &extra) == S31_TIMER_CLOSED);
    a.service = service;
    CHECK(pthread_attr_init(&attr) == 0);
    CHECK(pthread_attr_setstacksize(&attr, STACK_SIZE) == 0);
    CHECK(pthread_create(&thread, &attr, destroy_main, &a) == 0);
    CHECK(pthread_attr_destroy(&attr) == 0);
    /* No service accesses remain except the owner and active callback. */
    await(a.entered);
    no_event(a.joined, 50);
    SEM_OK(s31_sem_give(b.release));
    await(b.done);
    await(a.joined);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(a.result == S31_TIMER_OK && !a.service && atomic_load(&b.n) == 1);
    CHECK(atomic_load(&other.n) == 0);
    event_destroy(a.entered);
    event_destroy(a.joined);
    blocked_dispose(&b);
    blocked_dispose(&other);
    report("TIMER_CLOSE_DRAIN_PASS");
}

int main(void)
{
    alarm(30);
    order_test();
    periodic_test();
    inflight_test();
    close_test();
    alarm(0);
    report("S31_OSI_TIMER_PASS");
    return 0;
}
