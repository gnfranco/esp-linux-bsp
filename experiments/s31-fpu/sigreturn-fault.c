// SPDX-License-Identifier: Apache-2.0
/* RV32 F-only rt_sigreturn fault injection. Uses a traced disposable child. */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#define F_BIT (1UL << ('F'-'A'))
#define D_BIT (1UL << ('D'-'A'))
#define POISON UINT64_C(0xdeadbeef01234567)
struct fake_rt_frame {
    siginfo_t info;
    ucontext_t uc;
};
struct fpr {
    uint64_t regs[32];
    uint32_t fcsr, pad;
};
_Static_assert(sizeof(struct fpr) == 264, "RISC-V FP regset");
_Static_assert(sizeof(siginfo_t) == 128, "Linux signal info ABI");
_Static_assert(sizeof(((struct fake_rt_frame *)0)->uc.uc_mcontext.__fpregs.__d.__f[0]) == 8,
               "RISC-V 64-bit UAPI FP slots");
extern void fp_load(const uint32_t *, uint32_t);
extern void bad_rt_sigreturn(void *) __attribute__((noreturn));

static struct fake_rt_frame *fake;
static size_t uc_prefix;
static uint32_t pattern[32];

static void fault_handler(int signum, siginfo_t *info, void *arg)
{
    (void)signum;
    (void)info;
    /* Copy just the readable prefix of a genuine signal context. The
     * inaccessible page begins 16 bytes into the FP D-layout state. */
    memcpy(&fake->uc, arg, uc_prefix);
    fake->uc.uc_mcontext.__fpregs.__d.__f[0] = POISON;
    bad_rt_sigreturn(fake);
}

static int stopped(pid_t child, int sig)
{
    int st;
    pid_t ret;
    do {
        ret = waitpid(child, &st, 0);
    } while (ret < 0 && errno == EINTR);
    return ret == child && WIFSTOPPED(st) && WSTOPSIG(st) == sig;
}

static void child(void)
{
    long page = sysconf(_SC_PAGESIZE);
    size_t fp_off = offsetof(struct fake_rt_frame, uc.uc_mcontext.__fpregs.__d);
    size_t uc_off = offsetof(struct fake_rt_frame, uc);
    struct sigaction sa;
    unsigned char *mem;
    if (page <= 0 || fp_off + 16 >= (size_t)page || fp_off < uc_off)
        _exit(10);
    mem = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED || mprotect(mem + page, page, PROT_NONE))
        _exit(11);
    fake = (struct fake_rt_frame *)(mem + page - fp_off - 16);
    uc_prefix = fp_off + 16 - uc_off;
    if ((uintptr_t)fake & 15)
        _exit(12);
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) || ptrace(PTRACE_TRACEME, 0, 0, 0))
        _exit(13);
    for (unsigned int i = 0; i < 32; i++)
        pattern[i] = 0x31410000U | i;
    fp_load(pattern, 0x21);
    if (raise(SIGSTOP) || raise(SIGUSR1))
        _exit(14);
    _exit(15); /* Success must never resume normal child execution. */
}

static int get_fp(pid_t pid, struct fpr *p)
{
    struct iovec iov = { p, sizeof(*p) };
    return ptrace(PTRACE_GETREGSET, pid, (void *)(uintptr_t)NT_PRFPREG,
                  &iov) == 0 && iov.iov_len == sizeof(*p) && !p->pad;
}

int main(void)
{
    struct rlimit rl = {0, 0};
    struct fpr got;
    unsigned long hw = getauxval(AT_HWCAP);
    pid_t pid;
    int st, ok = 0;
    if (!(hw & F_BIT) || (hw & D_BIT))
        return 77;
    if (setrlimit(RLIMIT_CORE, &rl))
        return 1;
    pid = fork();
    if (pid < 0)
        return 1;
    if (!pid)
        child();
    if (!stopped(pid, SIGSTOP) ||
        ptrace(PTRACE_SETOPTIONS, pid, 0, (void *)(uintptr_t)PTRACE_O_EXITKILL))
        goto out;
    if (!get_fp(pid, &got) || got.regs[0] != UINT64_C(0x31410000))
        goto out;
    if (ptrace(PTRACE_CONT, pid, 0, 0) || !stopped(pid, SIGUSR1))
        goto out;
    if (ptrace(PTRACE_CONT, pid, 0, (void *)(uintptr_t)SIGUSR1) ||
        !stopped(pid, SIGSEGV) || !get_fp(pid, &got))
        goto out;
    printf("SIGRETURN_FAULT_REG0=%016llx\n", (unsigned long long)got.regs[0]);
    if ((uint32_t)got.regs[0] != (uint32_t)POISON) {
        puts("SIGRETURN_PARTIAL_WRITE_NOT_OBSERVED");
        goto out;
    }
    puts("SIGRETURN_PARTIAL_WRITE_OBSERVED");
    for (unsigned int i = 0; i < 32; i++)
        if (got.regs[i] >> 32)
            goto out;
    if (ptrace(PTRACE_CONT, pid, 0, (void *)(uintptr_t)SIGSEGV))
        goto out;
    if (waitpid(pid, &st, 0) != pid || !WIFSIGNALED(st) ||
        WTERMSIG(st) != SIGSEGV)
        goto out;
    puts("SIGRETURN_FAULT_PASS");
    ok = 1;
out:
    if (!ok) {
        /* Reap the tracee when any assertion fails. */
        kill(pid, SIGKILL);
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
            ;
        puts("SIGRETURN_FAULT_FAIL");
    }
    return ok ? 0 : 1;
}
