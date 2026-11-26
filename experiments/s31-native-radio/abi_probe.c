// SPDX-License-Identifier: Apache-2.0
/*
 * Deliberately freestanding RV32 Linux executable. Only reviewed pure
 * metadata getters are called: no PHY initialization, MMIO, IRQ or RTOS.
 */
#include <stdint.h>
#include <stddef.h>

extern const char *coex_version_get(void);
extern const char *r_btdm_get_compile_version(void);
extern uint32_t phy_get_rf_cal_version(void);

static long linux_syscall3(long nr, long x, long y, long z)
{
    register long a0 asm("a0") = x;
    register long a1 asm("a1") = y;
    register long a2 asm("a2") = z;
    register long a7 asm("a7") = nr;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

static int emit(const char *s, size_t n)
{
    while (n) {
        long r = linux_syscall3(64, 1, (long)s, (long)n);
        if (r <= 0 || (size_t)r > n)
            return -1;
        s += r;
        n -= (size_t)r;
    }
    return 0;
}

#define LIT(s) emit(s, sizeof(s) - 1)

static int metadata(const char *s)
{
    size_t n = 0;

    if (!s)
        return -1;
    while (n < 96 && s[n]) {
        if ((unsigned char)s[n] < 32 || (unsigned char)s[n] > 126)
            return -1;
        ++n;
    }
    if (!n || n == 96)
        return -1;
    return emit(s, n) || LIT("\n");
}

__attribute__((noreturn)) void _start(void)
{
    int bad = 0;

    bad |= LIT("COEX=");
    bad |= metadata(coex_version_get());
    bad |= LIT("BTDM=");
    bad |= metadata(r_btdm_get_compile_version());
    if (phy_get_rf_cal_version() != 100)
        bad = 1;
    else
        bad |= LIT("PHY_CAL_VERSION=100\n");
    if (!bad)
        bad |= LIT("S31_RADIO_ABI_PASS\n");
    else
        LIT("S31_RADIO_ABI_FAIL\n");

    linux_syscall3(93, bad != 0, 0, 0);
    for (;;)
        asm volatile("unimp");
}
