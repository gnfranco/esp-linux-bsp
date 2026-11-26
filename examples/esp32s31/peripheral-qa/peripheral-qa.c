// SPDX-License-Identifier: Apache-2.0
/* ESP32-S31 Linux read-only peripheral smoke tests. */
#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/netlink.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define ROUNDS 128

static int can_test(const char *name, int hardware, int brs, unsigned first_fd)
{
    const unsigned index = if_nametoindex(name);
    struct sockaddr_can addr = { .can_family = AF_CAN, .can_ifindex = index };
    int tx = -1, rx = -1, one = 1, zero = 0, rc = 1;
    if (!index) { perror("if_nametoindex"); return 1; }
    tx = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    rx = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (tx < 0 || rx < 0 ||
        setsockopt(tx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &one, sizeof(one)) ||
        setsockopt(rx, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &one, sizeof(one)) ||
        (hardware && setsockopt(tx, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &zero, sizeof(zero))) ||
        bind(tx, (struct sockaddr *)&addr, sizeof(addr)) ||
        bind(rx, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("SocketCAN setup");
        goto done;
    }
    for (unsigned fd = first_fd; fd <= 1; fd++) {
        for (unsigned i = 0; i < ROUNDS; i++) {
            struct canfd_frame out = { .can_id = (i & 1) ?
                (CAN_EFF_FLAG | 0x18da0000U | i) : (0x500U | i),
                .len = fd ? 64 : 8, .flags = fd && brs ? CANFD_BRS : 0 };
            struct canfd_frame in = {0};
            struct pollfd pfd = {.fd = rx, .events = POLLIN};
            size_t size = fd ? CANFD_MTU : CAN_MTU;
            for (unsigned j = 0; j < out.len; j++)
                out.data[j] = (uint8_t)(i * 13 + j * 7);
            if (write(tx, &out, size) != (ssize_t)size) {
                perror("SocketCAN send"); goto done;
            }
            if (poll(&pfd, 1, 1000) != 1 || !(pfd.revents & POLLIN)) {
                fprintf(stderr, "CAN timeout: fd=%u index=%u\n", fd, i); goto done;
            }
            if (read(rx, &in, sizeof(in)) != (ssize_t)size ||
                in.can_id != out.can_id || in.len != out.len ||
                (fd && (in.flags & CANFD_BRS) != (out.flags & CANFD_BRS)) ||
                memcmp(in.data, out.data, out.len)) {
                fprintf(stderr, "CAN mismatch: fd=%u index=%u\n", fd, i); goto done;
            }
        }
        printf("%s_%s_PASS %u/%u frames, %u-byte payload, SFF/EFF, BRS=%u\n",
               hardware ? "HARDWARE_LOOPBACK" : "SOCKETCAN", fd ? "FD" : "CLASSIC", ROUNDS, ROUNDS, fd ? 64 : 8, fd && brs);
    }
    rc = 0;
done:
    if (tx >= 0) close(tx);
    if (rx >= 0) close(rx);
    return rc;
}

static int codec_test(const char *dev, unsigned addr)
{
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    unsigned long funcs;
    if (fd < 0) { perror(dev); return 1; }
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0 || !(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr, "adapter lacks combined I2C transactions\n");
        close(fd); return 1;
    }
    unsigned version = 0;
    for (unsigned i = 0; i < ROUNDS; i++) {
        uint8_t id[3] = {0};
        for (unsigned j = 0; j < 3; j++) {
            uint8_t reg = 0xfd + j;
            struct i2c_msg msgs[2] = {
                { .addr = addr, .flags = 0, .len = 1, .buf = &reg },
                { .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &id[j] }
            };
            struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };
            /* Only set the register pointer; no codec register is modified. */
            if (ioctl(fd, I2C_RDWR, &xfer) != 2) {
                perror("I2C_RDWR"); close(fd); return 1;
            }
        }
        if (i == 0) version = id[2];
        if (id[0] != 0x83 || id[1] != 0x11 || id[2] != version) {
            fprintf(stderr, "unexpected codec ID/version: %02x %02x %02x\n",
                    id[0], id[1], id[2]); close(fd); return 1;
        }
    }
    printf("ES8311_I2C_PASS addr=0x%02x id=0x8311 version=0x%02x rounds=%u transactions=%u\n",
           addr, version, ROUNDS, ROUNDS * 3);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "can"))
        return can_test(argv[2], 0, 1, 0);
    if (argc == 3 && !strcmp(argv[1], "hardware-loopback"))
        return can_test(argv[2], 1, 1, 0);
    if (argc == 3 && !strcmp(argv[1], "hardware-fd-no-brs"))
        return can_test(argv[2], 1, 0, 1);
    if (argc == 3 && !strcmp(argv[1], "hardware-fd-brs"))
        return can_test(argv[2], 1, 1, 1);
    if (argc == 4 && !strcmp(argv[1], "codec")) {
        char *end;
        unsigned long addr = strtoul(argv[3], &end, 0);
        if (*end || (addr != 0x18 && addr != 0x19)) return 2;
        return codec_test(argv[2], (unsigned)addr);
    }
    fprintf(stderr, "usage: %s can vcan0 | hardware-loopback can0 | hardware-fd-no-brs can0 | hardware-fd-brs can0 | codec /dev/i2c-0 0x18|0x19\n", argv[0]);
    return 2;
}
