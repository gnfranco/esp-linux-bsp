/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-osi-mutex.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

enum { ITERATIONS = 10000 };

static wifi_osi_funcs_t table;
static atomic_bool ready;
static unsigned counter;

static void *contend(void *handle)
{
    assert(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
    assert(table._mutex_unlock(handle) == 0);
    atomic_store(&ready, true);
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        assert(table._mutex_lock(handle) == 1);
        ++counter;
        assert(table._mutex_unlock(handle) == 1);
    }
    return NULL;
}

static void test_contention(void)
{
    pthread_t worker;
    void *handle = table._recursive_mutex_create();

    assert(handle);
    assert(table._mutex_lock(handle) == 1);
    assert(table._mutex_lock(handle) == 1);
    assert(pthread_create(&worker, NULL, contend, handle) == 0);
    while (!atomic_load(&ready))
        sched_yield();
    assert(table._mutex_unlock(handle) == 1);
    assert(counter == 0);
    table._mutex_delete(handle);
    assert(radio_osi_mutex_faulted());
    assert(table._mutex_unlock(handle) == 1);

    for (unsigned i = 0; i < ITERATIONS; ++i) {
        assert(table._mutex_lock(handle) == 1);
        ++counter;
        assert(table._mutex_unlock(handle) == 1);
    }
    assert(pthread_join(worker, NULL) == 0);
    assert(counter == 2 * ITERATIONS);
    table._mutex_delete(handle);
    assert(table._mutex_lock(handle) == 0);
}

int main(void)
{
    void *handles[RADIO_OSI_MUTEX_CAPACITY];
    int foreign;

    assert(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
    assert(radio_osi_wire_mutex(NULL) == EINVAL);
    assert(radio_osi_wire_mutex(&table) == EINVAL);
    assert(table._mutex_create == NULL);
    table._version = ESP_WIFI_OS_ADAPTER_VERSION;
    table._magic = 123;
    assert(radio_osi_wire_mutex(&table) == 0);
    assert(table._version == ESP_WIFI_OS_ADAPTER_VERSION);
    assert(table._magic == 123 && table._spin_lock_create == NULL);

    for (unsigned i = 0; i < RADIO_OSI_MUTEX_CAPACITY; ++i) {
        handles[i] = table._mutex_create();
        assert(handles[i]);
    }
    assert(table._recursive_mutex_create() == NULL);
    assert(table._mutex_unlock(handles[0]) == 0);
    assert(table._mutex_lock(handles[0]) == 1);
    assert(table._mutex_lock(handles[0]) == 0);
    assert(table._mutex_unlock(handles[0]) == 1);
    assert(table._mutex_lock(NULL) == 0);
    assert(table._mutex_unlock(&foreign) == 0);

    table._mutex_delete(handles[0]);
    void *replacement = table._mutex_create();

    assert(replacement && replacement != handles[0]);
    assert(table._mutex_lock(handles[0]) == 0);
    assert(table._mutex_unlock(handles[0]) == 0);
    table._mutex_delete(replacement);
    for (unsigned i = 1; i < RADIO_OSI_MUTEX_CAPACITY; ++i)
        table._mutex_delete(handles[i]);

    assert(!radio_osi_mutex_faulted());
    test_contention();
    table._mutex_delete(NULL);
    assert(radio_osi_mutex_faulted());
    return 0;
}
