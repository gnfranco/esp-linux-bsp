/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "wifi-userspace-log.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stdout, "FAIL line %d: %s (errno=%d)\n", __LINE__, #condition, errno); \
        exit(1); \
    } \
} while (0)

static void test_logger(int (*logger)(const char *, ...), int saved_stderr)
{
    char long_text[8193];
    static const char tail[] = "|   abc|-42|0x2a|-1234567890123|1.25|\0|%  \r\n";
    char actual[sizeof(long_text) + sizeof(tail)];
    memset(long_text, 'q', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';

    char capture_path[] = "test-stderr.XXXXXX";
    int capture_fd = mkstemp(capture_path);
    CHECK(capture_fd >= 0);
    CHECK(unlink(capture_path) == 0);
    FILE *capture = fdopen(capture_fd, "w+");
    CHECK(capture != NULL);
    CHECK(dup2(fileno(capture), STDERR_FILENO) == STDERR_FILENO);

    size_t len = sizeof(long_text) - 1 + sizeof(tail) - 1;
    CHECK(logger("%s", "") == 0);
    CHECK(logger("%c", 'X') == 1);
    CHECK(logger("%s|%*.*s|%d|%#x|%lld|%.2f|%c|%% %s\r\n",
                 long_text, 6, 3, "abcdef", -42, 0x2au,
                 -1234567890123LL, 1.25, 0, " ") == (int)len);
    CHECK(fflush(stderr) == 0);
    CHECK(dup2(saved_stderr, STDERR_FILENO) == STDERR_FILENO);
    CHECK(fseek(capture, 0, SEEK_SET) == 0);
    CHECK(fread(actual, 1, sizeof(actual), capture) == len + 1);
    CHECK(feof(capture) && !ferror(capture));
    CHECK(actual[0] == 'X');
    CHECK(memcmp(actual + 1, long_text, sizeof(long_text) - 1) == 0);
    CHECK(memcmp(actual + sizeof(long_text), tail, sizeof(tail) - 1) == 0);
    CHECK(fclose(capture) == 0);

    errno = 0;
    CHECK(logger("%lc", (wint_t)0xd800) < 0 && errno == EILSEQ);
    clearerr(stderr);

    int full = open("/dev/full", O_WRONLY);
    CHECK(full >= 0);
    CHECK(dup2(full, STDERR_FILENO) == STDERR_FILENO);
    CHECK(close(full) == 0);
    errno = 0;
    CHECK(logger("%s", "write failure") < 0 && errno == ENOSPC && ferror(stderr));
    CHECK(dup2(saved_stderr, STDERR_FILENO) == STDERR_FILENO);
    clearerr(stderr);
}

int main(void)
{
    CHECK(setlocale(LC_ALL, "C") != NULL);
    CHECK(setvbuf(stderr, NULL, _IONBF, 0) == 0);
    int saved_stderr = dup(STDERR_FILENO);
    CHECK(saved_stderr >= 0);
    test_logger(net80211_printf, saved_stderr);
    test_logger(phy_printf, saved_stderr);
    test_logger(pp_printf, saved_stderr);
    CHECK(close(saved_stderr) == 0);
    puts("S31_WIFI_USERSPACE_LOG_TEST_PASS");
    return 0;
}
