/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_RADIO_OSI_MUTEX_H
#define S31_RADIO_OSI_MUTEX_H

#include "wifi_os_adapter.h"

#define RADIO_OSI_MUTEX_CAPACITY 32

/* Linux pthread context only. Handles are opaque tokens. Release all owned
 * locks before thread exit. Calls require cancellation to be disabled.
 * Create returns NULL when the pool is exhausted. Lock waits for other
 * owners and returns 1 on success, 0 on failure. Unlock returns the same
 * status. Invalid or busy delete latches a process-lifetime fault and leaves
 * the pool intact. An idle handle may be deleted and its slot reused.
 * Wire only the five mutex fields of a caller-owned, unpublished v9 table.
 */
int radio_osi_wire_mutex(wifi_osi_funcs_t *table);
int radio_osi_mutex_faulted(void);

#endif
