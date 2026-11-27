/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "radio-sram-backend.h"
#include "radio-osi-alloc.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s (errno=%d)\n",__LINE__,#x,errno); exit(1); } } while (0)
static wifi_osi_funcs_t w;
static coex_adapter_funcs_t c;
static struct radio_sram_backend s;
static int inside(const void *p, size_t n)
{
    uintptr_t x=(uintptr_t)p, base=(uintptr_t)s.base;
    return p && x>=base && x-base<S31_SRAM_BYTES && n<=S31_SRAM_BYTES-(x-base);
}
static void zeros(const unsigned char *p,size_t n)
{
    for(size_t i=0;i<n;++i) CHECK(!p[i]);
}
static void *worker(void *arg)
{
    (void)arg;
    for(unsigned i=0;i<256;++i) {
        unsigned char *p=w._calloc_internal(2,32);
        CHECK(inside(p,64)); zeros(p,64);
        memset(p,0x33,64); w._free(p);
    }
    return NULL;
}
int main(int argc,char **argv)
{
    CHECK(argc==2 && (!strcmp(argv[1],"host") || !strcmp(argv[1],"device")));
    int device=!strcmp(argv[1],"device"),fd=-1;
    if(device) {
        CHECK(sysconf(_SC_PAGESIZE)==4096);
        fd=open("/dev/s31-wifi-sram-lab",O_RDWR|O_CLOEXEC);
        CHECK(fd>=0);
    }
    void *base=mmap(NULL,S31_SRAM_BYTES,PROT_READ|PROT_WRITE,
                    device?MAP_SHARED:MAP_PRIVATE|MAP_ANONYMOUS,fd,0);
    CHECK(base!=MAP_FAILED);
    struct radio_backend b;
    CHECK(!radio_sram_init(&s,base,S31_SRAM_BYTES,&b));
    zeros(base,S31_SRAM_BYTES);
    CHECK(!radio_osi_wire_alloc(&w,&c));
    /* Deliberately allow one extra unit in the ledger to exercise
     * the backend's own hard capacity limit. Not a production budget. */
    const size_t limits[]={4096,0,S31_SRAM_BYTES+S31_SRAM_UNIT};
    CHECK(!radio_osi_open(&b,limits,S31_SRAM_BYTES+S31_SRAM_UNIT));
    unsigned char *g=w._wifi_malloc(128);
    CHECK(g && !inside(g,1));
    memset(g,0x77,128);
    unsigned char *p=w._malloc_internal(1600);
    unsigned char *q=w._calloc_internal(1,500);
    CHECK(inside(p,1600) && inside(q,500) && p!=q);
    CHECK(!((uintptr_t)p%64) && !((uintptr_t)q%64));
    CHECK(s.live_internal==2112 && s.live_general>=128);
    zeros(q,500);
    memset(p,0xa5,1600);
    if(device) {
        unsigned char v[64];
        CHECK(pread(fd,v,sizeof(v),p-s.base)==sizeof(v));
        for(size_t i=0;i<sizeof(v);++i)CHECK(v[i]==0xa5);
    }
    CHECK(!w._realloc_internal(p,S31_SRAM_BYTES));
    CHECK(p[1599]==0xa5 && s.live_internal==2112);
    size_t old_offset=(size_t)(p-s.base);
    unsigned char *r=w._realloc_internal(p,2048);
    CHECK(inside(r,2048) && r[1599]==0xa5);
    zeros(s.base+old_offset,1600);
    CHECK(g[127]==0x77);
    w._free(q);w._free(r);w._free(g);
    CHECK(radio_sram_idle(&s));
    zeros(base,S31_SRAM_BYTES);
    puts("SRAM_OSI_CLASSES_REALLOC_PASS");
    /* Two free extents: enough total space, no contiguous 8 KiB. */
    unsigned char *a[4];
    for(unsigned i=0;i<4;++i){a[i]=w._malloc_internal(4096);CHECK(inside(a[i],4096));}
    CHECK(!w._malloc_internal(1) && s.live_internal==S31_SRAM_BYTES);
    w._free(a[0]);w._free(a[2]);
    CHECK(!w._malloc_internal(8192) && s.live_internal==8192);
    w._free(a[1]);w._free(a[3]);
    p=w._malloc_internal(S31_SRAM_BYTES);
    CHECK(p==base && !w._malloc_internal(1));
    w._free(p);
    CHECK(radio_sram_idle(&s));
    puts("SRAM_OSI_EXHAUSTION_FRAGMENTATION_PASS");
    pthread_t t[4];
    for(unsigned i=0;i<4;++i) CHECK(!pthread_create(&t[i],NULL,worker,NULL));
    for(unsigned i=0;i<4;++i) CHECK(!pthread_join(t[i],NULL));
    CHECK(!radio_osi_faulted() && radio_sram_idle(&s));
    CHECK(s.peak_internal==S31_SRAM_BYTES);
    zeros(base,S31_SRAM_BYTES);
    CHECK(!radio_osi_close());
    CHECK(munmap(base,S31_SRAM_BYTES)==0);
    if(device) {
        CHECK(close(fd)==0);
        CHECK(open("/dev/s31-wifi-sram-lab",O_RDWR|O_CLOEXEC)==-1 && errno==EBUSY);
    }
    puts("SRAM_OSI_THREADS_CLEANUP_PASS");
    puts(device?"S31_SRAM_OSI_DEVICE_PASS":"S31_SRAM_OSI_HOST_PASS");
    return 0;
}
