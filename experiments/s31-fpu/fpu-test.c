// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <elf.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#define F_BIT (1UL << ('F' - 'A'))
#define D_BIT (1UL << ('D' - 'A'))
#define HWPROBE_KEY_IMA_EXT_0 4
#define HWPROBE_IMA_FD 1ULL
#ifndef __NR_riscv_hwprobe
#define __NR_riscv_hwprobe 258
#endif
struct hwprobe_pair { int64_t key; uint64_t value; };
struct fpr { uint64_t regs[32]; uint32_t fcsr, pad; };
_Static_assert(sizeof(struct fpr) == 264, "NT_PRFPREG layout mismatch");
extern void fp_load(const uint32_t *, uint32_t);
extern uint32_t fp_store(uint32_t *);
extern uint32_t fp_add(uint32_t, uint32_t);

static uint32_t expected[32], other[32];
static volatile sig_atomic_t handled;
static int cpus[2];

static void die(const char *s) { perror(s); exit(1); }
static void fail(const char *s) { fprintf(stderr, "FAIL: %s\n", s); exit(1); }
static void pattern(uint32_t *p, unsigned int seed)
{
    for (unsigned i = 0; i < 32; i++)
        p[i] = 0x3f000000U | ((seed * 1031U + i * 257U) & 0x7fffffU);
}
static void check(const uint32_t *want, uint32_t csr)
{
    uint32_t got[32]; uint32_t actual = fp_store(got);
    if (memcmp(want, got, sizeof(got)) || actual != csr)
        fail("FP register/FCSR mismatch");
}
static void pin(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set)) die("sched_setaffinity");
    if (sched_getcpu() != cpu) fail("did not migrate to selected hart");
}
static void signal_clobber(int sig)
{
    (void)sig;
    fp_load(other, 0x42);
    handled = 1;
}
static void worker(unsigned seed)
{
    pattern(expected, seed);
    pin(cpus[0]);
    fp_load(expected, 0x21);
    for (int i = 0; i < 128; i++) {
        pin(cpus[i & 1]);
        if (sched_yield()) die("sched_yield");
        check(expected, 0x21);
    }
    printf("SMP_CONTEXT_PASS seed=%u harts=%d,%d\n", seed, cpus[0], cpus[1]);
}
static void wait_child(pid_t pid)
{
    int st;
    if (waitpid(pid, &st, 0) != pid) die("waitpid");
    if (!WIFEXITED(st) || WEXITSTATUS(st)) fail("child failed");
}
static void ptrace_test(void)
{
    int st;
    pattern(expected, 17); pattern(other, 19);
    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (!pid) {
        if (ptrace(PTRACE_TRACEME, 0, 0, 0)) die("TRACEME");
        fp_load(expected, 0x21);
        if (raise(SIGSTOP)) die("raise");
        check(other, 0x42);
        if (raise(SIGSTOP)) die("raise");
        _exit(0);
    }
    if (waitpid(pid, &st, 0) != pid || !WIFSTOPPED(st) ||
        WSTOPSIG(st) != SIGSTOP) fail("initial ptrace stop");
    struct fpr state;
    struct iovec iov = { &state, sizeof(state) };
    if (ptrace(PTRACE_GETREGSET, pid, (void *)(uintptr_t)NT_PRFPREG, &iov))
        die("GETREGSET");
    if (iov.iov_len != sizeof(state) || state.fcsr != 0x21 || state.pad)
        fail("ptrace layout/FCSR");
    for (unsigned i = 0; i < 32; i++) {
        if (state.regs[i] != expected[i]) fail("ptrace initial registers");
        state.regs[i] = 0xa5a5a5a500000000ULL | other[i];
    }
    state.fcsr = 0x42;
    if (ptrace(PTRACE_SETREGSET, pid, (void *)(uintptr_t)NT_PRFPREG, &iov))
        die("SETREGSET");
    if (ptrace(PTRACE_CONT, pid, 0, 0)) die("CONT");
    if (waitpid(pid, &st, 0) != pid || !WIFSTOPPED(st) ||
        WSTOPSIG(st) != SIGSTOP) fail("second ptrace stop");
    iov.iov_len = sizeof(state);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)(uintptr_t)NT_PRFPREG, &iov))
        die("GETREGSET");
    for (unsigned i = 0; i < 32; i++)
        if (state.regs[i] != other[i]) fail("ptrace restored registers/padding");
    if (state.fcsr != 0x42 || state.pad) fail("ptrace restored FCSR/pad");
    if (ptrace(PTRACE_CONT, pid, 0, 0)) die("CONT");
    wait_child(pid);
    puts("PTRACE_PASS");
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    unsigned long hw = getauxval(AT_HWCAP);
    struct hwprobe_pair p = { HWPROBE_KEY_IMA_EXT_0, 0 };
    if (syscall(__NR_riscv_hwprobe, &p, 1, 0, NULL, 0))
        die("riscv_hwprobe");
    printf("HWCAP=0x%lx F=%d D=%d HWPROBE_FD=%d\n", hw, !!(hw & F_BIT),
           !!(hw & D_BIT), !!(p.value & HWPROBE_IMA_FD));
    if (p.key != HWPROBE_KEY_IMA_EXT_0) fail("invalid hwprobe key");
    if (!!(p.value & HWPROBE_IMA_FD) != !!((hw & F_BIT) && (hw & D_BIT)))
        fail("HWCAP/hwprobe FD mismatch");
    if (!(hw & F_BIT)) {
        puts("FPU_NOT_ADVERTISED: hardware opcodes not executed");
        return 77;
    }
    if (hw & D_BIT) fail("F-only qualification requires F without D");
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set)) die("sched_getaffinity");
    int n = 0;
    for (int i = 0; i < CPU_SETSIZE && n < 2; i++)
        if (CPU_ISSET(i, &set)) cpus[n++] = i;
    if (n != 2) fail("need two online harts");
    for (int i = 0; i < 2; i++) {
        pin(cpus[i]);
        if (fp_add(0x3f800000, 0x40000000) != 0x40400000)
            fail("FADD.S result");
        printf("FADD_PASS hart=%d\n", cpus[i]);
    }
    pattern(expected, 1); pattern(other, 2);
    struct sigaction sa = { .sa_handler = signal_clobber };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL)) die("sigaction");
    fp_load(expected, 0x21);
    if (raise(SIGUSR1) || !handled) fail("signal delivery");
    check(expected, 0x21);
    puts("SIGNAL_PASS");

    pid_t children[2];
    for (unsigned i = 0; i < 2; i++) {
        children[i] = fork();
        if (children[i] < 0) die("fork");
        if (!children[i]) { worker(i + 3); _exit(0); }
    }
    for (unsigned i = 0; i < 2; i++) wait_child(children[i]);
    ptrace_test();
    puts("S31_F_ONLY_PASS");
    return 0;
}
