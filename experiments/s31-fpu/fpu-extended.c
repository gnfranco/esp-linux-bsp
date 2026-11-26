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
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define F_BIT (1UL << ('F'-'A'))
#define D_BIT (1UL << ('D'-'A'))
#define NITER 2048
extern void fp_load(const uint32_t *, uint32_t);
extern uint32_t fp_store(uint32_t *);
extern uint32_t fp_misaligned(const void *, void *);
extern uint32_t fp_misaligned_c(const void *, void *);
extern void fp_forbidden_d(void);
extern void fp_forbidden_ld(const void *);
extern void fp_forbidden_sd(void *);
extern char **environ;
static uint32_t a[32], b[32];
static volatile sig_atomic_t handler_ok, phase;
static int cpus[2];
static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}
static void die(const char *msg) { perror(msg); exit(1); }
static void pattern(uint32_t *p, unsigned seed)
{
    uint32_t x = seed | 1U;
    for (unsigned i=0; i<32; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        p[i] = x;
    }
}
static void check(const uint32_t *p, uint32_t csr)
{
    uint32_t got[32], csr_got=fp_store(got);
    for (unsigned i=0; i<32; i++)
        if (got[i]!=p[i]) {
            fprintf(stderr,"REG%u expected=%08" PRIx32 " actual=%08" PRIx32 "\n",i,p[i],got[i]);
            fail("FP register mismatch");
        }
    if (csr_got!=csr) fail("FCSR mismatch");
}
static void wait_good(pid_t pid)
{
    int st;
    if (waitpid(pid,&st,0)!=pid) die("waitpid");
    if (!WIFEXITED(st) || WEXITSTATUS(st)) {
        fprintf(stderr,"child status=0x%x\n",st);
        fail("child failure");
    }
}
static void pin(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu,&set);
    if (sched_setaffinity(0,sizeof(set),&set)) die("sched_setaffinity");
    if (sched_getcpu()!=cpu) fail("wrong hart after affinity");
}
static void mutate_frame(int sig, siginfo_t *info, void *arg)
{
    (void)sig; (void)info;
    ucontext_t *uc=arg;
    struct __riscv_mc_d_ext_state *p=&uc->uc_mcontext.__fpregs.__d;
    const uint32_t *want=phase==1 ? a : b;
    unsigned csr=phase==1 ? 0x21U : 0x63U;
    handler_ok=0;
    for (unsigned i=0; i<32; i++)
        if (p->__f[i]!=want[i]) return;
    if (p->__fcsr!=csr) return;
    if (phase==1) {
        for (unsigned i=0; i<32; i++) p->__f[i]=0xa5a5a5a500000000ULL|b[i];
        p->__fcsr=0x63;
        fp_load(a,0x42); /* live clobber must not override edited frame */
    }
    handler_ok=1;
}
static void signal_frames(void)
{
    struct sigaction sa;
    memset(&sa,0,sizeof(sa)); sa.sa_sigaction=mutate_frame; sa.sa_flags=SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1,&sa,NULL)) die("sigaction");
    pattern(a,0x17); pattern(b,0x23);
    fp_load(a,0x21);
    phase=1;
    if (raise(SIGUSR1)||!handler_ok) fail("initial signal frame");
    check(b,0x63);
    phase=2;
    if (raise(SIGUSR1)||!handler_ok) fail("canonical signal frame");
    check(b,0x63);
    puts("SIGNAL_FRAME_MUTATION_PASS");
}
static void fork_exec(void)
{
    pattern(a,0x341); pattern(b,0x197);
    fp_load(a,0x43);
    pid_t pid=fork();
    if (pid<0) die("fork");
    if (!pid) {
        check(a,0x43);
        fp_load(b,0x21);
        check(b,0x21);
        _exit(0);
    }
    wait_good(pid); check(a,0x43); puts("FORK_ISOLATION_PASS");
    pid=fork();
    if (pid<0) die("fork");
    if (!pid) {
        fp_load(b,0x63);
        char *const argv[]={(char *)"fpu-extended",(char *)"--exec-child",NULL};
        execve("/proc/self/exe",argv,environ);
        die("execve");
    }
    wait_good(pid); check(a,0x43); puts("EXEC_RESET_PASS");
}
struct fpr { uint64_t regs[32]; uint32_t fcsr,pad; };
_Static_assert(sizeof(struct fpr)==264,"regset");
static void getregs(pid_t pid,struct fpr *p)
{
    struct iovec iov={p,sizeof(*p)};
    if (ptrace(PTRACE_GETREGSET,pid,(void *)(uintptr_t)NT_PRFPREG,&iov)) die("GETREGSET");
    if (iov.iov_len!=sizeof(*p) || p->pad) fail("regset length/pad");
}
static void ptrace_fault(void)
{
    pid_t pid; int st;
    pattern(a,0x77);pattern(b,0x91);
    pid=fork();
    if (pid<0) die("fork");
    if (!pid) {
        if (ptrace(PTRACE_TRACEME,0,0,0)) die("TRACEME");
        fp_load(a,0x21);
        raise(SIGSTOP);
        check(b,0x42);
        _exit(0);
    }
    if (waitpid(pid,&st,0)!=pid || !WIFSTOPPED(st)) fail("trace stop");
    if (ptrace(PTRACE_SETOPTIONS,pid,0,(void *)(uintptr_t)PTRACE_O_EXITKILL))
        die("EXITKILL");
    struct fpr regs;
    getregs(pid,&regs);
    for (unsigned i=0;i<32;i++) if (regs.regs[i]!=a[i]) fail("initial regset");
    long page=sysconf(_SC_PAGESIZE);
    if (page<=0) fail("page size");
    char *m=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (m==MAP_FAILED) die("mmap");
    if (mprotect(m+page,(size_t)page,PROT_NONE)) die("mprotect");
    uint64_t val=0xdeadbeef01234567ULL;
    memcpy(m+page-8,&val,8);
    struct iovec fault={m+page-8,16};
    errno=0;
    if (ptrace(PTRACE_SETREGSET,pid,(void *)(uintptr_t)NT_PRFPREG,&fault)!=-1 || errno!=EFAULT)
        fail("faulting SETREGSET did not return EFAULT");
    getregs(pid,&regs);
    int canonical=1;
    for (unsigned i=0;i<32;i++) if (regs.regs[i]>>32) canonical=0;
    printf("PTRACE_FAULT_CANONICAL=%d\n",canonical);
    if (munmap(m,(size_t)page*2)) die("munmap");
    for (unsigned i=0;i<32;i++) regs.regs[i]=0xa5a5a5a500000000ULL|b[i];
    regs.fcsr=0x42; regs.pad=0;
    struct iovec good={&regs,sizeof(regs)};
    if (ptrace(PTRACE_SETREGSET,pid,(void *)(uintptr_t)NT_PRFPREG,&good)) die("SETREGSET");
    getregs(pid,&regs);
    for (unsigned i=0;i<32;i++) if (regs.regs[i]!=b[i]) fail("immediate canonical SETREGSET");
    if (regs.fcsr!=0x42) fail("immediate fcsr");
    if (ptrace(PTRACE_CONT,pid,0,0)) die("CONT");
    wait_good(pid);
    puts("PTRACE_IMMEDIATE_PASS");
    if (!canonical) puts("PTRACE_FAULT_INVARIANT_GAP");
    else puts("PTRACE_FAULT_PASS");
}
static void stress_worker(unsigned id)
{
    uint32_t want[32];
    uint32_t csr=(((id%5U)<<5)|(id&31U));
    pin(cpus[id&1]);
    pattern(want,0x1111U+id);
    fp_load(want,csr);
    for (unsigned i=0;i<NITER;i++) {
        if (id>=2) pin(cpus[(i+id)&1]);
        if (sched_yield()) die("yield");
        check(want,csr);
        if ((i&63U)==63U) {
            pattern(want,0x10001U+id*65537U+i);
            csr=(((i/64U+id)%5U)<<5)|(i&31U);
            fp_load(want,csr);
        }
    }
    printf("STRESS_PASS worker=%u mode=%s checks=%u\n",id,id>=2?"migrating":"pinned",NITER);
    _exit(0);
}
static void stress(void)
{
    pid_t p[4];
    struct timespec start,end;
    if (clock_gettime(CLOCK_MONOTONIC,&start)) die("clock");
    for (unsigned i=0;i<4;i++) {
        p[i]=fork();
        if (p[i]<0) die("fork stress");
        if (!p[i]) stress_worker(i);
    }
    for(unsigned i=0;i<4;i++) wait_good(p[i]);
    if (clock_gettime(CLOCK_MONOTONIC,&end)) die("clock");
    long ms=(end.tv_sec-start.tv_sec)*1000L+(end.tv_nsec-start.tv_nsec)/1000000L;
    printf("SMP_STRESS_PASS checks=%u elapsed_ms=%ld\n",4U*NITER,ms);
}
typedef uint32_t (*unaligned_fn)(const void *,void *);
static void misaligned_child(unaligned_fn fn)
{
    _Alignas(8) unsigned char src[16],dst[16];
    uint32_t val=0xc1234567U,got;
    memset(src,0,sizeof(src)); memset(dst,0x5a,sizeof(dst));
    memcpy(src+1,&val,4);
    uint32_t raw=fn(src+1,dst+3);
    memcpy(&got,dst+3,4);
    if (got!=val || raw!=val || dst[2]!=0x5a || dst[7]!=0x5a) fail("unaligned FP access value");
    _exit(0);
}
static void misaligned(unaligned_fn fn,const char *name)
{
    int st; pid_t pid=fork();
    if (pid<0) die("fork");
    if (!pid) misaligned_child(fn);
    if (waitpid(pid,&st,0)!=pid) die("waitpid");
    if (WIFEXITED(st)&&!WEXITSTATUS(st)) printf("MISALIGNED_%s_PASS\n",name);
    else if (WIFSIGNALED(st)&&WTERMSIG(st)==SIGBUS) printf("MISALIGNED_%s_SIGBUS\n",name);
    else fail("unexpected misaligned result");
}
static void no_double(unsigned type)
{
    pid_t pid=fork(); int st;
    if (pid<0) die("fork");
    if (!pid) {
        _Alignas(8) uint64_t data[2]={0};
        if(type==0) fp_forbidden_d();
        else if(type==1) fp_forbidden_ld(data);
        else if(type==2) fp_forbidden_sd(data);
        else if(type==3) fp_forbidden_ld((char *)data+1);
        else fp_forbidden_sd((char *)data+1);
        _exit(0);
    }
    if(waitpid(pid,&st,0)!=pid) die("waitpid");
    if(!WIFSIGNALED(st) || (WTERMSIG(st)!=SIGILL && !(type>=3&&WTERMSIG(st)==SIGBUS)))
        fail("D instruction did not fault safely");
    printf("D_REJECT_PASS case=%u signal=%d\n",type,WTERMSIG(st));
}
int main(int argc,char **argv)
{
    unsigned long hw=getauxval(AT_HWCAP);
    if (!(hw&F_BIT) || (hw&D_BIT)) return 77;
    if (argc==2 && !strcmp(argv[1],"--exec-child")) {
        uint32_t zero[32]={0};
        check(zero,0);
        return 0;
    }
    if (argc!=1) return 2;
    setvbuf(stdout,NULL,_IOLBF,0);
    struct rlimit rl={0,0};
    if(setrlimit(RLIMIT_CORE,&rl)) die("setrlimit");
    cpu_set_t set;
    if(sched_getaffinity(0,sizeof(set),&set)) die("affinity");
    int n=0;
    for(int i=0;i<CPU_SETSIZE&&n<2;i++) if(CPU_ISSET(i,&set)) cpus[n++]=i;
    if(n!=2) fail("need two harts");
    signal_frames();
    fork_exec();
    ptrace_fault();
    for(unsigned i=0;i<5;i++) no_double(i);
    misaligned(fp_misaligned,"F");
    misaligned(fp_misaligned_c,"ZCF");
    stress();
    puts("S31_F_EXTENDED_PASS");
    return 0;
}
