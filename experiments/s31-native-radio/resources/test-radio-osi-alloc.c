/* SPDX-License-Identifier: Apache-2.0 */
#include "radio-osi-alloc.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static size_t live[RADIO_CLASS_COUNT];
static int fail;
static int quote(void *ctx, enum radio_class c, size_t n, size_t a, size_t *out)
{
    (void)ctx; (void)c;
    if (n > SIZE_MAX - (a-1)) return -1;
    *out = (n+a-1)&~(a-1);
    return 0;
}
static void *acquire(void *ctx, enum radio_class c, size_t n, size_t a, size_t charge)
{
    (void)ctx; (void)n;
    if (fail) return NULL;
    void *p = aligned_alloc(a,charge);
    if (p) { live[c]+=charge; memset(p,0xa5,charge); }
    return p;
}
static void release(void *ctx, enum radio_class c, void *p, size_t charge)
{
    (void)ctx; CHECK(live[c]>=charge); live[c]-=charge; free(p);
}
_Static_assert(__builtin_types_compatible_p(__typeof__(&radio_osi_bt_malloc),
                                           __typeof__(&wr_btdm_osal_malloc)), "BT malloc signature");
_Static_assert(__builtin_types_compatible_p(__typeof__(&radio_osi_free),
                                           __typeof__(&wr_btdm_osal_free)), "BT free signature");
static wifi_osi_funcs_t w;
static coex_adapter_funcs_t c;
static void *worker(void *arg)
{
    (void)arg;
    for (unsigned i=0;i<256;++i) {
        void *p=w._wifi_calloc(4,16);
        CHECK(p && ((unsigned char *)p)[63]==0);
        w._free(p);
    }
    return NULL;
}
int main(int argc, char **argv)
{
    /* Only these fields are wired. Test tables are never registered. */
    w._version=123; c._version=456;
    CHECK(radio_osi_wire_alloc(&w,&c)==0);
    CHECK(w._version==123 && c._version==456 && !w._get_free_heap_size && !c._timer_done);
    CHECK(!w._malloc(32));
    struct radio_backend b={NULL,quote,acquire,release};
    const size_t limits[]={4096,0,1024};
    if (argc==2 && !strcmp(argv[1],"preopen")) {
        int foreign=0; w._free(&foreign);
        CHECK(radio_osi_faulted() && radio_osi_open(&b,limits,1024)==EPROTO);
        puts("S31_RADIO_OSI_PREOPEN_PASS"); return 0;
    }
    CHECK(radio_osi_open(&b,limits,1024)==0);
    if (argc==2 && !strcmp(argv[1],"foreign")) {
        int foreign=0; void *p=w._malloc(32); CHECK(p && !radio_osi_faulted());
        w._free(&foreign); CHECK(radio_osi_faulted() && !w._malloc(32));
        w._free(p); CHECK(!radio_osi_close());
        puts("S31_RADIO_OSI_FOREIGN_PASS"); return 0;
    }
    if (argc==2 && !strcmp(argv[1],"postclose")) {
        CHECK(!radio_osi_faulted() && !radio_osi_close() && !w._malloc(32));
        int foreign=0; w._free(&foreign); CHECK(radio_osi_faulted());
        CHECK(radio_osi_open(&b,limits,1024)==EBUSY);
        puts("S31_RADIO_OSI_POSTCLOSE_PASS"); return 0;
    }
    CHECK(argc==1);
    CHECK(radio_osi_open(&b,limits,1024)==EBUSY);
    CHECK(!w._wifi_malloc(0) && !w._wifi_calloc(SIZE_MAX,2));
    CHECK(!radio_osi_bt_malloc(32,(btdm_osal_malloc_flag_t)99));
    void *p=w._wifi_realloc(NULL,128);
    CHECK(p && live[RADIO_GENERAL]>=128);
    memset(p,0x33,128);
    void *q=c._malloc_internal(128);
    CHECK(q && live[RADIO_INTERNAL_DMA]>=128);
    CHECK(radio_osi_close()==EBUSY);
    fail=1;
    CHECK(!w._wifi_realloc(p,256) && ((unsigned char *)p)[127]==0x33);
    fail=0;
    p=w._wifi_realloc(p,256);
    CHECK(p && ((unsigned char *)p)[127]==0x33);
    CHECK(!w._wifi_realloc(p,0) && !live[RADIO_GENERAL]);
    w._free(q);
    q=wr_btdm_osal_malloc(64,BTDM_OSAL_MALLOC_F_INTERNAL);
    p=wr_btdm_osal_malloc(64,BTDM_OSAL_MALLOC_F_NORMAL);
    CHECK(q && p && live[RADIO_INTERNAL_DMA] && live[RADIO_GENERAL]);
    c._free(p); wr_btdm_osal_free(q);
    p=w._malloc(32); CHECK(p); w._free(p);
    p=w._wifi_zalloc(32); CHECK(p && !((unsigned char *)p)[31]); c._free(p);
    p=w._malloc_internal(32); CHECK(p); radio_osi_free(p);
    p=w._calloc_internal(2,32); CHECK(p && !((unsigned char *)p)[63]);
    memset(p,0x45,64);
    p=w._realloc_internal(p,128); CHECK(p && ((unsigned char *)p)[63]==0x45);
    CHECK(!w._realloc_internal(p,0) && !live[RADIO_INTERNAL_DMA]);
    pthread_t workers[4];
    for (unsigned i=0;i<4;++i) CHECK(!pthread_create(&workers[i],NULL,worker,NULL));
    for (unsigned i=0;i<4;++i) CHECK(!pthread_join(workers[i],NULL));
    CHECK(!radio_osi_faulted());
    p=w._zalloc_internal(64); CHECK(p && !((unsigned char *)p)[63]);
    CHECK(!w._wifi_realloc(p,128) && radio_osi_faulted());
    CHECK(!w._malloc(32));
    w._free(p);
    int foreign=0; w._free(&foreign); w._free(NULL);
    for (unsigned i=0;i<RADIO_CLASS_COUNT;++i) CHECK(live[i]==0);
    CHECK(!radio_osi_close());
    CHECK(!w._malloc(32) && radio_osi_open(&b,limits,1024)==EBUSY);
    puts("S31_RADIO_OSI_ALLOC_PASS");
    return 0;
}
