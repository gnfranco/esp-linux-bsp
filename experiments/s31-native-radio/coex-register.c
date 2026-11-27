// SPDX-License-Identifier: Apache-2.0
/*
 * Registration-only test of the pinned vendor coex archive. The ROM
 * pointer is represented by private process storage. This is NOT a
 * functional adapter: callbacks are deliberately NULL and never invoked.
 * No RF initialization, ROM calls, hardware mappings, threads or IRQs.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "esp_coexist_adapter.h"

_Static_assert(sizeof(void *) == 4, "RV32 pointer ABI required");
_Static_assert(sizeof(coex_adapter_funcs_t) == 80, "unexpected table size");
_Static_assert(offsetof(coex_adapter_funcs_t, _magic) == 76, "unexpected magic offset");
_Static_assert(COEX_ADAPTER_VERSION == 2, "unexpected version");
_Static_assert(COEX_ADAPTER_MAGIC == 0xDEADBEAF, "unexpected magic");

extern int esp_coex_adapter_register(coex_adapter_funcs_t *funcs);
coex_adapter_funcs_t *g_coa_funcs_p;
static coex_adapter_funcs_t good = {
    ._version = COEX_ADAPTER_VERSION, ._magic = COEX_ADAPTER_MAGIC
};
static coex_adapter_funcs_t wrong_version = {
    ._version = 3, ._magic = COEX_ADAPTER_MAGIC
};
static coex_adapter_funcs_t wrong_magic = {
    ._version = COEX_ADAPTER_VERSION, ._magic = 0
};
static unsigned put_count, printf_count;
static int log_error;

static long syscall3(long nr, long x, long y, long z)
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
        long rc = syscall3(64, 1, (long)s, (long)n);
        if (rc <= 0 || (size_t)rc > n)
            return -1;
        s += rc;
        n -= (size_t)rc;
    }
    return 0;
}

#define LIT(s) emit(s, sizeof(s) - 1)

static int bounded_string(const char *s)
{
    size_t n = 0;
    if (!s)
        return -1;
    while (n < 160 && s[n])
        ++n;
    if (n == 160 || emit(s, n))
        return -1;
    return (int)n;
}

/* Real diagnostics for the reviewed call graph, not success stubs. */
int puts(const char *s)
{
    int n = bounded_string(s);
    ++put_count;
    if (n < 0 || LIT("\n")) {
        log_error = 1;
        return -1;
    }
    return n + 1;
}

int coexist_printf(const char *fmt, ...)
{
    va_list args;
    int n = 0;
    ++printf_count;
    va_start(args, fmt);
    for (size_t i = 0; fmt && i < 160; ++i) {
        char ch = fmt[i];
        if (!ch) {
            va_end(args);
            if (LIT("\n")) {
                log_error = 1;
                return -1;
            }
            return n;
        }
        if (ch == '%') {
            static const char digits[] = "0123456789abcdef";
            char buf[8];
            size_t at = sizeof(buf);
            unsigned int x;
            /* Registration's two error formats use only %x. Reject others. */
            if (++i >= 160 || fmt[i] != 'x')
                break;
            x = va_arg(args, unsigned int);
            do {
                buf[--at] = digits[x & 15];
                x >>= 4;
            } while (x);
            if (emit(buf + at, sizeof(buf) - at))
                break;
            n += (int)(sizeof(buf) - at);
        } else {
            if (emit(&ch, 1))
                break;
            ++n;
        }
    }
    va_end(args);
    log_error = 1;
    return -1;
}

__attribute__((noreturn)) static void finish(int failed)
{
    if (failed)
        LIT("S31_COEX_REGISTER_FAIL\n");
    else if (LIT("S31_COEX_REGISTER_PASS\n"))
        failed = 1;
    syscall3(93, failed, 0, 0);
    for (;;)
        asm volatile("unimp");
}

#define REQUIRE(expr, marker) do { \
    if (!(expr) || LIT(marker "\n")) finish(1); \
} while (0)

__attribute__((noreturn)) void _start(void)
{
    /* All rejections must run before any successful registration. */
    REQUIRE(!g_coa_funcs_p, "COEX_EMPTY");
    REQUIRE(esp_coex_adapter_register(NULL) == 0x102 &&
            !g_coa_funcs_p && put_count == 1 && !log_error, "COEX_NULL_REJECTED");
    REQUIRE(esp_coex_adapter_register(&wrong_version) == 0x102 &&
            !g_coa_funcs_p && printf_count == 1 && !log_error, "COEX_VERSION_REJECTED");
    REQUIRE(esp_coex_adapter_register(&wrong_magic) == 0x102 &&
            !g_coa_funcs_p && printf_count == 2 && !log_error, "COEX_MAGIC_REJECTED");
    REQUIRE(esp_coex_adapter_register(&good) == 0 &&
            g_coa_funcs_p == &good, "COEX_REGISTERED_PRIVATE_POINTER");

    /* Vendor returns success after first registration without revalidation. */
    REQUIRE(esp_coex_adapter_register(&wrong_version) == 0 &&
            g_coa_funcs_p == &good && printf_count == 2, "COEX_REPLACE_IGNORED");
    REQUIRE(esp_coex_adapter_register(NULL) == 0 &&
            g_coa_funcs_p == &good && put_count == 1, "COEX_NULL_AFTER_REGISTER_IGNORED");
    finish(log_error);
}
