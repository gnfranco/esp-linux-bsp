/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-osi-alloc.h"

/* Only for the selected S31 multi-link profile. Non-multilink has extra
 * block-memory hooks and is not supported. Link only when this adapter
 * exclusively owns the selected controller's allocation callbacks.
 */
#if !CONFIG_BT_CTRL_MULTI_LINK_ENABLED
#error "Non-multilink BTDM block-memory API not implemented"
#endif
void *wr_btdm_osal_malloc(uint32_t size, btdm_osal_malloc_flag_t flags)
{
    return radio_osi_bt_malloc(size, flags);
}
void wr_btdm_osal_free(void *ptr)
{
    radio_osi_free(ptr);
}
