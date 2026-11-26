// SPDX-License-Identifier: Apache-2.0
/* Bounded anon-VMA split/fork/COW workload; no file or device writes. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int wait_ok(pid_t pid)
{
    int status;
    pid_t ret;

    do {
        ret = waitpid(pid, &status, 0);
    } while (ret < 0 && errno == EINTR);
    return ret == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int cycle(unsigned int iteration, size_t page)
{
    unsigned char *p = mmap(NULL, page * 3, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char a = (unsigned char)iteration;
    pid_t child;
    int ok = 0;

    if (p == MAP_FAILED)
        return 0;
    p[0] = a;
    p[page] = a ^ 0xa5;
    p[page * 2] = a ^ 0x5a;
    child = fork();
    if (child < 0)
        goto out;
    if (child == 0) {
        /* Split a populated private VMA with an inherited anon_vma. */
        if (mprotect(p + page, page, PROT_READ) ||
            p[0] != a || p[page] != (unsigned char)(a ^ 0xa5) ||
            p[page * 2] != (unsigned char)(a ^ 0x5a) ||
            mprotect(p + page, page, PROT_READ | PROT_WRITE))
            _exit(1);
        p[page] ^= 0xff;
        _exit(p[page] == (unsigned char)(a ^ 0x5a) ? 0 : 2);
    }
    /* Exercise the parent's separate VMA while the child splits its own. */
    ok = mprotect(p + page, page, PROT_READ) == 0;
    if (ok)
        ok = p[page] == (unsigned char)(a ^ 0xa5) &&
             mprotect(p + page, page, PROT_READ | PROT_WRITE) == 0;
    ok = wait_ok(child) && ok;
    ok = ok && p[0] == a && p[page] == (unsigned char)(a ^ 0xa5) &&
         p[page * 2] == (unsigned char)(a ^ 0x5a);
out:
    return munmap(p, page * 3) == 0 && ok;
}

static int worker(unsigned int n, size_t page)
{
    for (unsigned int i = 0; i < n; i++) {
        if (!cycle(i, page)) {
            fprintf(stderr, "ANON_VMA_FAIL iteration=%u pid=%ld\n",
                    i, (long)getpid());
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    char *end;
    unsigned long n;
    long page = sysconf(_SC_PAGESIZE);
    pid_t other;
    int ok;

    if (argc != 2 || page <= 0) {
        fprintf(stderr, "usage: %s CYCLES_PER_WORKER (1..256)\n", argv[0]);
        return 2;
    }
    errno = 0;
    n = strtoul(argv[1], &end, 10);
    if (errno || end == argv[1] || *end || n < 1 || n > 256)
        return 2;
    other = fork();
    if (other < 0) {
        perror("fork worker");
        return 1;
    }
    if (other == 0)
        _exit(worker((unsigned int)n, (size_t)page) ? 0 : 1);
    ok = worker((unsigned int)n, (size_t)page);
    ok = wait_ok(other) && ok;
    if (!ok)
        return 1;
    printf("ANON_VMA_PASS workers=2 cycles=%lu page=%ld\n", 2 * n, page);
    return 0;
}
