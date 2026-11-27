/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEVICE "/dev/s31-wifi-sram-lab"
#define BYTES 16384
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s (errno=%d)\n", __LINE__, #x, errno); exit(1); } } while (0)

/* Serial handoff: verify both harts actually execute the test. */
static void on_cpu(int cpu)
{
    cpu_set_t mask;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    CHECK(sched_setaffinity(0, sizeof(mask), &mask) == 0);
    CHECK(sched_getcpu() == cpu);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static void check_zero(const unsigned char *p)
{
    for (size_t i = 0; i < BYTES; ++i)
        CHECK(p[i] == 0);
}

int main(void)
{
    CHECK(sysconf(_SC_PAGESIZE) == 4096);
    on_cpu(0);
    int fd = open(DEVICE, O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    CHECK(open(DEVICE, O_RDWR | O_CLOEXEC) == -1 && errno == EBUSY);
    unsigned char buf[64], expect[64];
    memset(expect, 0x7c, sizeof(expect));
    CHECK(pwrite(fd, expect, sizeof(expect), 128) == sizeof(expect));
    on_cpu(1);
    CHECK(pread(fd, buf, sizeof(buf), 128) == sizeof(buf));
    CHECK(!memcmp(buf, expect, sizeof(buf)));
    CHECK(close(fd) == 0);
    fd = open(DEVICE, O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    CHECK(pread(fd, buf, sizeof(buf), 128) == sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i)
        CHECK(buf[i] == 0);
    puts("SRAM_SESSION_COPY_ONLY_REUSE_PASS");
    on_cpu(0);
    CHECK(mmap(NULL, BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0) == MAP_FAILED);
    CHECK(mmap(NULL, BYTES, PROT_READ, MAP_SHARED, fd, 4096) == MAP_FAILED);
    CHECK(mmap(NULL, BYTES + 4096, PROT_READ, MAP_SHARED, fd, 0) == MAP_FAILED);
    CHECK(mmap(NULL, BYTES, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0) == MAP_FAILED);
    unsigned char *p = mmap(NULL, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(p != MAP_FAILED);
    CHECK(mprotect(p, BYTES, PROT_READ | PROT_EXEC) == -1);
    check_zero(p);

    memset(expect, 0x5a, sizeof(expect));
    memset(p + 128, 0x5a, sizeof(expect));
    on_cpu(1);
    CHECK(pread(fd, buf, sizeof(buf), 128) == sizeof(buf));
    CHECK(!memcmp(buf, expect, sizeof(buf)));
    memset(expect, 0xa5, sizeof(expect));
    CHECK(pwrite(fd, expect, sizeof(expect), 256) == sizeof(expect));
    on_cpu(0);
    CHECK(!memcmp(p + 256, expect, sizeof(expect)));
    CHECK(pread(fd, buf, sizeof(buf), BYTES) == 0);
    CHECK(pwrite(fd, expect, sizeof(expect), BYTES - 8) == 8);
    CHECK(!memcmp(p + BYTES - 8, expect, 8));
    puts("SRAM_SESSION_CPU_CROSS_VIEW_PASS");
    puts("SRAM_SESSION_CROSS_HART_PASS");

    CHECK(close(fd) == 0);
    CHECK(p[128] == 0x5a && p[256] == 0xa5);
    CHECK(open(DEVICE, O_RDWR | O_CLOEXEC) == -1 && errno == EBUSY);
    CHECK(munmap(p, BYTES) == 0);
    puts("SRAM_SESSION_MAPPING_LIFETIME_PASS");

    /* Mapped extents are deliberately retained until reboot. */
    CHECK(open(DEVICE, O_RDWR | O_CLOEXEC) == -1 && errno == EBUSY);
    puts("SRAM_SESSION_QUARANTINE_PASS");
    puts("S31_WIFI_SRAM_SESSION_PASS");
    return 0;
}
