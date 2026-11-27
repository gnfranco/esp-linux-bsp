/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_RADIO_OSI_ALLOC_H
#define S31_RADIO_OSI_ALLOC_H
#include "radio-alloc.h"
#include "wifi_os_adapter.h"
#include "esp_coexist_adapter.h"
#include "btdm_osal.h"

/* One service lifetime per process. Task context only, no cancellation,
 * signal handlers, callback reentry or outstanding payload borrowers on
 * free/realloc. Caller drains vendor threads before close; close refuses
 * live allocations. Backend/context must remain valid until close.
 * Pointer callbacks cannot detect stale pointers whose addresses were reused.
 * Invalid free or unknown/class-mismatched realloc latches a protocol fault;
 * subsequent allocation requests fail; valid frees remain available.
 * A pre-open fault prevents opening the service (EPROTO).
 * Zero-size malloc/calloc return NULL; realloc(p,0) frees a valid same-class
 * p. realloc(NULL,n) allocates. No implicit capability migration.
 * Patch only allocation fields of caller-owned, unpublished tables.
 * Does not register, version, or certify completeness of either table.
 */
int radio_osi_open(const struct radio_backend *, const size_t limits[RADIO_CLASS_COUNT],
                   size_t internal_limit);
int radio_osi_close(void);
int radio_osi_faulted(void);
int radio_osi_wire_alloc(wifi_osi_funcs_t *, coex_adapter_funcs_t *);
void *radio_osi_bt_malloc(uint32_t, btdm_osal_malloc_flag_t);
void radio_osi_free(void *);
#endif
