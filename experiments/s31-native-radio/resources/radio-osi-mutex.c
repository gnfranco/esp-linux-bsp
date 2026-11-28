/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-osi-mutex.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>

_Static_assert(ESP_WIFI_OS_ADAPTER_VERSION == 9, "expected Wi-Fi OS adapter v9");

struct mutex_slot {
    uintptr_t token;
    pthread_t owner;
    unsigned depth;
    unsigned waiters;
    bool live;
    bool recursive;
};

static struct mutex_slot slots[RADIO_OSI_MUTEX_CAPACITY];
static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t released = PTHREAD_COND_INITIALIZER;
static bool faulted;

/* All slot state is protected by guard. Token arithmetic never wraps. */
static struct mutex_slot *find_slot(void *handle)
{
    uintptr_t token = (uintptr_t)handle;

    if (!token)
        return NULL;
    struct mutex_slot *slot = &slots[(token - 1) % RADIO_OSI_MUTEX_CAPACITY];
    return slot->live && slot->token == token ? slot : NULL;
}

static void *create(bool recursive)
{
    void *handle = NULL;

    pthread_mutex_lock(&guard);
    for (unsigned i = 0; i < RADIO_OSI_MUTEX_CAPACITY; ++i) {
        struct mutex_slot *slot = &slots[i];

        if (slot->live || slot->token > UINTPTR_MAX - RADIO_OSI_MUTEX_CAPACITY)
            continue;
        slot->token = slot->token ? slot->token + RADIO_OSI_MUTEX_CAPACITY : i + 1;
        slot->live = true;
        slot->recursive = recursive;
        handle = (void *)slot->token;
        break;
    }
    pthread_mutex_unlock(&guard);
    return handle;
}

static void *mutex_create(void)
{
    return create(false);
}

static void *recursive_mutex_create(void)
{
    return create(true);
}

static void mutex_delete(void *handle)
{
    pthread_mutex_lock(&guard);
    struct mutex_slot *slot = find_slot(handle);

    if (!slot || slot->depth || slot->waiters)
        faulted = true;
    else
        slot->live = false;
    pthread_mutex_unlock(&guard);
}

static int32_t mutex_lock(void *handle)
{
    pthread_t self = pthread_self();
    int32_t success = 0;

    pthread_mutex_lock(&guard);
    struct mutex_slot *slot = find_slot(handle);

    if (!slot)
        goto out;
    if (slot->depth && pthread_equal(slot->owner, self)) {
        if (!slot->recursive || slot->depth == UINT_MAX)
            goto out;
    } else if (slot->depth) {
        int rc;

        if (slot->waiters == UINT_MAX)
            goto out;
        ++slot->waiters;
        do {
            rc = pthread_cond_wait(&released, &guard);
        } while (!rc && slot->depth);
        --slot->waiters;
        if (rc)
            goto out;
    }
    slot->owner = self;
    ++slot->depth;
    success = 1;
out:
    pthread_mutex_unlock(&guard);
    return success;
}

static int32_t mutex_unlock(void *handle)
{
    int32_t success = 0;

    pthread_mutex_lock(&guard);
    struct mutex_slot *slot = find_slot(handle);

    if (slot && slot->depth && pthread_equal(slot->owner, pthread_self())) {
        if (!--slot->depth && slot->waiters)
            pthread_cond_broadcast(&released);
        success = 1;
    }
    pthread_mutex_unlock(&guard);
    return success;
}

int radio_osi_mutex_faulted(void)
{
    pthread_mutex_lock(&guard);
    int result = faulted;
    pthread_mutex_unlock(&guard);
    return result;
}

int radio_osi_wire_mutex(wifi_osi_funcs_t *table)
{
    if (!table || table->_version != ESP_WIFI_OS_ADAPTER_VERSION)
        return EINVAL;
    table->_mutex_create = mutex_create;
    table->_recursive_mutex_create = recursive_mutex_create;
    table->_mutex_delete = mutex_delete;
    table->_mutex_lock = mutex_lock;
    table->_mutex_unlock = mutex_unlock;
    return 0;
}
