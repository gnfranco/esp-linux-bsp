/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_RADIO_SRAM_BACKEND_H
#define S31_RADIO_SRAM_BACKEND_H
#include "radio-alloc.h"
#define S31_SRAM_BYTES 16384u
#define S31_SRAM_UNIT 64u
#define S31_SRAM_UNITS (S31_SRAM_BYTES / S31_SRAM_UNIT)

/* CPU-only lab backend. Caller owns exclusive mapping for whole lifetime.
 * Use only under radio_osi's serialization; no DMA despite logical class
 * names. Never register vendor tables. Drain all borrowers, close OSI,
 * verify idle, then unmap. No implicit class fallback. Metadata lives
 * outside SRAM. GENERAL uses the ordinary process heap.
 */
struct radio_sram_backend {
    unsigned char *base;
    unsigned char occupied[S31_SRAM_UNITS];
    size_t live_general, live_internal, peak_internal;
    unsigned live_blocks;
};
int radio_sram_init(struct radio_sram_backend *, void *mapping, size_t bytes,
                    struct radio_backend *);
int radio_sram_idle(const struct radio_sram_backend *);
#endif
