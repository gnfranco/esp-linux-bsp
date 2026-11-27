/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "osi-sem.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) fail(#x); } while (0)
#define OK(x) CHECK((x) == S31_SEM_OK)
#define NTHREADS 4
#define ROUNDS 1024

static void report(const char *s)
{
    (void)write(STDOUT_FILENO, s, strlen(s));
    (void)write(STDOUT_FILENO, "\n", 1);
}

static void fail(const char *s)
{
    report("S31_OSI_SEM_FAIL");
    report(s);
    _Exit(1);
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
           (uint64_t)ts.tv_nsec;
}

static void pause_ms(void)
{
    struct timespec ts = { .tv_nsec = 1000000 };
    while (nanosleep(&ts, &ts) && errno == EINTR)
        ;
}

static void waiters(s31_sem *sem, uint32_t expected)
{
    uint64_t end = now_ns() + UINT64_C(5000000000);
    for (;;) {
        struct s31_sem_snapshot snap;
        OK(s31_sem_snapshot(sem, &snap));
        if (snap.waiters == expected)
            return;
        CHECK(now_ns() < end);
        pause_ms();
    }
}

static void spawn(pthread_t *thread, void *(*fn)(void *), void *arg)
{
    pthread_attr_t attr;
    CHECK(pthread_attr_init(&attr) == 0);
#ifdef S31_TEST_LARGE_STACK
    CHECK(pthread_attr_setstacksize(&attr, 1024 * 1024) == 0);
#else
    CHECK(pthread_attr_setstacksize(&attr, 64 * 1024) == 0);
#endif
    CHECK(pthread_create(thread, &attr, fn, arg) == 0);
    CHECK(pthread_attr_destroy(&attr) == 0);
}

static void dispose(s31_sem *sem)
{
    OK(s31_sem_close(sem));
    OK(s31_sem_destroy(sem));
}

static void capacity_test(void)
{
    s31_sem *sem = NULL;
    struct s31_sem_snapshot snap;
    CHECK(s31_sem_create(1, 0, 100, NULL) == S31_SEM_INVALID);
    CHECK(s31_sem_create(0, 0, 100, &sem) == S31_SEM_INVALID && !sem);
    CHECK(s31_sem_create(1, 2, 100, &sem) == S31_SEM_INVALID && !sem);
    CHECK(s31_sem_create(1, 0, 0, &sem) == S31_SEM_INVALID && !sem);
    CHECK(s31_sem_create(1, 0, UINT32_MAX, &sem) == S31_SEM_INVALID && !sem);
    CHECK(s31_sem_take(NULL, 0) == S31_SEM_INVALID);
    CHECK(s31_sem_give(NULL) == S31_SEM_INVALID);
    CHECK(s31_sem_close(NULL) == S31_SEM_INVALID);
    CHECK(s31_sem_destroy(NULL) == S31_SEM_INVALID);
    CHECK(s31_sem_snapshot(NULL, &snap) == S31_SEM_INVALID);
    OK(s31_sem_create(2, 1, 100, &sem));
    CHECK(s31_sem_snapshot(sem, NULL) == S31_SEM_INVALID);
    OK(s31_sem_snapshot(sem, &snap));
    CHECK(snap.count == 1 && snap.capacity == 2 && !snap.closed && !snap.waiters);
    OK(s31_sem_give(sem));
    CHECK(s31_sem_give(sem) == S31_SEM_FULL);
    OK(s31_sem_take(sem, 0));
    OK(s31_sem_take(sem, 0));
    CHECK(s31_sem_take(sem, 0) == S31_SEM_WOULD_BLOCK);
    CHECK(s31_sem_destroy(sem) == S31_SEM_BUSY);
    OK(s31_sem_give(sem));
    OK(s31_sem_close(sem));
    OK(s31_sem_close(sem));
    /* Closure wins, even with a token still available. */
    CHECK(s31_sem_take(sem, 0) == S31_SEM_CLOSED);
    CHECK(s31_sem_give(sem) == S31_SEM_CLOSED);
    OK(s31_sem_snapshot(sem, &snap));
    CHECK(snap.closed && snap.count == 1 && !snap.waiters);
    OK(s31_sem_destroy(sem));

    OK(s31_sem_create(UINT32_MAX, UINT32_MAX, 100, &sem));
    CHECK(s31_sem_give(sem) == S31_SEM_FULL);
    OK(s31_sem_take(sem, 0));
    OK(s31_sem_give(sem));
    OK(s31_sem_snapshot(sem, &snap));
    CHECK(snap.count == UINT32_MAX);
    dispose(sem);
    report("SEM_CAPACITY_PASS");
}

static void timeout_test(void)
{
    uint64_t start, elapsed;
    s31_sem *sem;
    OK(s31_sem_create(1, 0, 100, &sem));
    start = now_ns();
    CHECK(s31_sem_take(sem, 5) == S31_SEM_TIMEOUT);
    elapsed = now_ns() - start;
    CHECK(elapsed >= UINT64_C(50000000) && elapsed < UINT64_C(5000000000));
    waiters(sem, 0);
    dispose(sem);

    /* Fractional tick rate, deadline rounds up. */
    OK(s31_sem_create(1, 0, 333, &sem));
    start = now_ns();
    CHECK(s31_sem_take(sem, 1) == S31_SEM_TIMEOUT);
    CHECK(now_ns() - start >= UINT64_C(3003004));
    dispose(sem);
    report("SEM_MONOTONIC_TIMEOUT_PASS");
}

struct take_arg {
    s31_sem *sem;
    uint32_t ticks;
    enum s31_sem_result result;
};

static void *take_one(void *v)
{
    struct take_arg *arg = v;
    arg->result = s31_sem_take(arg->sem, arg->ticks);
    return NULL;
}

static void wake_test(void)
{
    pthread_t thread;
    s31_sem *sem;
    struct take_arg arg;
    uint64_t start;
    OK(s31_sem_create(1, 0, 1000, &sem));
    arg = (struct take_arg) { .sem = sem, .ticks = 10000 };
    spawn(&thread, take_one, &arg);
    waiters(sem, 1);
    start = now_ns();
    OK(s31_sem_give(sem));
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(now_ns() - start < UINT64_C(5000000000));
    CHECK(arg.result == S31_SEM_OK);
    waiters(sem, 0);
    CHECK(s31_sem_take(sem, 0) == S31_SEM_WOULD_BLOCK);
    dispose(sem);
    report("SEM_WAKE_PASS");
}

static void close_test(void)
{
    pthread_t threads[NTHREADS];
    struct take_arg arg[NTHREADS];
    s31_sem *sem;
    uint64_t start;
    OK(s31_sem_create(2, 0, 1000, &sem));
    for (unsigned i = 0; i < NTHREADS; ++i) {
        arg[i] = (struct take_arg) { .sem = sem,
            .ticks = i % 2 ? 10000 : S31_SEM_FOREVER };
        spawn(&threads[i], take_one, &arg[i]);
    }
    waiters(sem, NTHREADS);
    start = now_ns();
    OK(s31_sem_close(sem));
    for (unsigned i = 0; i < NTHREADS; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(arg[i].result == S31_SEM_CLOSED);
    }
    CHECK(now_ns() - start < UINT64_C(5000000000));
    waiters(sem, 0);
    OK(s31_sem_close(sem));
    OK(s31_sem_destroy(sem));
    report("SEM_CLOSE_WAKE_ALL_PASS");
}

static void cancellation_test(void)
{
    pthread_t threads[2];
    struct take_arg arg[2];
    void *ret;
    s31_sem *sem;
    OK(s31_sem_create(1, 0, 1000, &sem));
    for (unsigned i = 0; i < 2; ++i) {
        arg[i] = (struct take_arg) { .sem = sem, .ticks = S31_SEM_FOREVER };
        spawn(&threads[i], take_one, &arg[i]);
    }
    waiters(sem, 2);
    CHECK(pthread_cancel(threads[0]) == 0);
    CHECK(pthread_join(threads[0], &ret) == 0);
    CHECK(ret == PTHREAD_CANCELED);
    waiters(sem, 1);
    /* Cancellation must not poison the lock or steal the next give. */
    OK(s31_sem_give(sem));
    CHECK(pthread_join(threads[1], NULL) == 0);
    CHECK(arg[1].result == S31_SEM_OK);
    waiters(sem, 0);
    CHECK(s31_sem_take(sem, 0) == S31_SEM_WOULD_BLOCK);
    dispose(sem);
    report("SEM_DEFERRED_CANCEL_PASS");
}

struct stress {
    s31_sem *sem;
    atomic_uint produced;
    atomic_uint consumed;
};

static void *producer(void *v)
{
    struct stress *s = v;
    for (unsigned i = 0; i < ROUNDS; ++i) {
        enum s31_sem_result rc;
        do {
            rc = s31_sem_give(s->sem);
            if (rc == S31_SEM_FULL)
                sched_yield();
        } while (rc == S31_SEM_FULL);
        CHECK(rc == S31_SEM_OK);
        atomic_fetch_add(&s->produced, 1);
    }
    return NULL;
}

static void *consumer(void *v)
{
    struct stress *s = v;
    for (unsigned i = 0; i < ROUNDS; ++i) {
        OK(s31_sem_take(s->sem, S31_SEM_FOREVER));
        atomic_fetch_add(&s->consumed, 1);
    }
    return NULL;
}

static void stress_test(void)
{
    struct stress s = { .produced = 0, .consumed = 0 };
    struct s31_sem_snapshot snap;
    pthread_t threads[NTHREADS];
    OK(s31_sem_create(7, 0, 1000, &s.sem));
    for (unsigned i = 0; i < 2; ++i) {
        spawn(&threads[i], consumer, &s);
        spawn(&threads[i + 2], producer, &s);
    }
    for (unsigned i = 0; i < NTHREADS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(atomic_load(&s.produced) == 2 * ROUNDS);
    CHECK(atomic_load(&s.consumed) == 2 * ROUNDS);
    OK(s31_sem_snapshot(s.sem, &snap));
    CHECK(snap.count == 0 && !snap.waiters && !snap.closed);
    dispose(s.sem);
    report("SEM_MPMC_2048_PASS");
}

/* Exercise races with close while a producer and consumer are active. */
struct close_race {
    s31_sem *sem;
    atomic_uint produced;
    atomic_uint consumed;
};

static void *race_producer(void *v)
{
    struct close_race *s = v;
    for (;;) {
        enum s31_sem_result rc = s31_sem_give(s->sem);
        if (rc == S31_SEM_CLOSED)
            return NULL;
        if (rc == S31_SEM_FULL)
            sched_yield();
        else {
            CHECK(rc == S31_SEM_OK);
            atomic_fetch_add(&s->produced, 1);
        }
    }
}

static void *race_consumer(void *v)
{
    struct close_race *s = v;
    for (;;) {
        enum s31_sem_result rc = s31_sem_take(s->sem, S31_SEM_FOREVER);
        if (rc == S31_SEM_CLOSED)
            return NULL;
        CHECK(rc == S31_SEM_OK);
        atomic_fetch_add(&s->consumed, 1);
    }
}

static void close_race_test(void)
{
    for (unsigned i = 0; i < 16; ++i) {
        struct close_race s = { .produced = 0, .consumed = 0 };
        struct s31_sem_snapshot snap;
        pthread_t a, b;
        uint64_t end = now_ns() + UINT64_C(5000000000);
        OK(s31_sem_create(1, 0, 1000, &s.sem));
        spawn(&a, race_producer, &s);
        spawn(&b, race_consumer, &s);
        while (atomic_load(&s.produced) < 16) {
            CHECK(now_ns() < end);
            pause_ms();
        }
        OK(s31_sem_close(s.sem));
        CHECK(pthread_join(a, NULL) == 0);
        CHECK(pthread_join(b, NULL) == 0);
        OK(s31_sem_snapshot(s.sem, &snap));
        CHECK(snap.closed && !snap.waiters && snap.count <= 1);
        CHECK(atomic_load(&s.produced) ==
              atomic_load(&s.consumed) + snap.count);
        CHECK(s31_sem_give(s.sem) == S31_SEM_CLOSED);
        CHECK(s31_sem_take(s.sem, 0) == S31_SEM_CLOSED);
        OK(s31_sem_destroy(s.sem));
    }
    report("SEM_CLOSE_RACE_16_PASS");
}

/* Cancel and give in both orders while two waiters are registered. */
static void cancel_race_test(void)
{
    for (unsigned i = 0; i < 16; ++i) {
        pthread_t threads[2];
        struct take_arg arg[2];
        struct s31_sem_snapshot snap;
        void *ret;
        s31_sem *sem;
        OK(s31_sem_create(1, 0, 1000, &sem));
        for (unsigned j = 0; j < 2; ++j) {
            arg[j] = (struct take_arg) { .sem = sem,
                .ticks = S31_SEM_FOREVER, .result = S31_SEM_BUSY };
            spawn(&threads[j], take_one, &arg[j]);
        }
        waiters(sem, 2);
        if (i % 2)
            OK(s31_sem_give(sem));
        CHECK(pthread_cancel(threads[0]) == 0);
        if (!(i % 2))
            OK(s31_sem_give(sem));
        CHECK(pthread_join(threads[0], &ret) == 0);
        if (arg[0].result == S31_SEM_OK) {
            /* First waiter completed its take before cancellation. */
            OK(s31_sem_close(sem));
            CHECK(pthread_join(threads[1], NULL) == 0);
            CHECK(arg[1].result == S31_SEM_CLOSED);
        } else {
            /* Cancellation during wait must not consume the only token. */
            CHECK(ret == PTHREAD_CANCELED);
            CHECK(pthread_join(threads[1], NULL) == 0);
            CHECK(arg[1].result == S31_SEM_OK);
        }
        OK(s31_sem_snapshot(sem, &snap));
        CHECK(!snap.count && !snap.waiters);
        dispose(sem);
    }
    report("SEM_CANCEL_RACE_16_PASS");
}

int main(void)
{
    alarm(30);
    capacity_test();
    timeout_test();
    wake_test();
    close_test();
    cancellation_test();
    stress_test();
    close_race_test();
    cancel_race_test();
    alarm(0);
    report("S31_OSI_SEM_PASS");
    return 0;
}
