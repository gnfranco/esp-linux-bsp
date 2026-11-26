// SPDX-License-Identifier: Apache-2.0
/* Configure an isolated CAN controller for internal hardware loopback. */
#include <errno.h>
#include <linux/can/netlink.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

struct request {
    struct nlmsghdr header;
    struct ifinfomsg link;
    char buf[256];
};

static struct rtattr *attr(struct request *r, unsigned short type,
                           const void *data, unsigned short size)
{
    struct rtattr *a = (void *)((char *)r + NLMSG_ALIGN(r->header.nlmsg_len));
    size_t end = NLMSG_ALIGN(r->header.nlmsg_len) + RTA_ALIGN(RTA_LENGTH(size));
    if (end > sizeof(*r)) return NULL;
    a->rta_type = type;
    a->rta_len = RTA_LENGTH(size);
    if (size) memcpy(RTA_DATA(a), data, size);
    r->header.nlmsg_len = end;
    return a;
}

static void nest_end(struct request *r, struct rtattr *a)
{
    a->rta_len = (char *)r + r->header.nlmsg_len - (char *)a;
}

static int link_state(const char *name, int up)
{
    struct ifreq req = {0};
    int sock = socket(AF_INET, SOCK_DGRAM, 0), ret = -1;
    if (sock < 0) return -1;
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", name);
    if (ioctl(sock, SIOCGIFFLAGS, &req) < 0) goto out;
    if (up) req.ifr_flags |= IFF_UP;
    else req.ifr_flags &= ~IFF_UP;
    ret = ioctl(sock, SIOCSIFFLAGS, &req);
out:
    close(sock);
    return ret;
}

int main(int argc, char **argv)
{
    struct request r = { .header = {
        .nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
        .nlmsg_type = RTM_NEWLINK, .nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
        .nlmsg_seq = 1,
    }, .link = { .ifi_family = AF_UNSPEC } };
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    struct can_bittiming bt = { .bitrate = 250000 };
    struct can_bittiming dbt = { .bitrate = 1000000 };
    struct can_ctrlmode cm = {
        .mask = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_FD | CAN_CTRLMODE_ONE_SHOT | CAN_CTRLMODE_PRESUME_ACK | CAN_CTRLMODE_BERR_REPORTING,
        .flags = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_FD | CAN_CTRLMODE_ONE_SHOT | CAN_CTRLMODE_PRESUME_ACK | CAN_CTRLMODE_BERR_REPORTING,
    };
    struct rtattr *info, *data;
    unsigned char buf[4096];
    ssize_t len;
    int sock, rc = 1;
    if ((argc != 2 && argc != 3) || (strcmp(argv[1],"can0") && strcmp(argv[1],"can1"))) return 2;
    if (argc == 3) {
        char *end;
        unsigned long x = strtoul(argv[2], &end, 0);
        if (*end || x < 250000 || x > 2000000) return 2;
        dbt.bitrate = x;
    }
    r.link.ifi_index = if_nametoindex(argv[1]);
    if (!r.link.ifi_index || link_state(argv[1], 0) < 0) {
        perror("interface down"); return 1;
    }
    info = attr(&r, IFLA_LINKINFO | NLA_F_NESTED, NULL, 0);
    if (!info || !attr(&r, IFLA_INFO_KIND, "can", 4)) return 1;
    data = attr(&r, IFLA_INFO_DATA | NLA_F_NESTED, NULL, 0);
    if (!data || !attr(&r, IFLA_CAN_CTRLMODE, &cm, sizeof(cm)) ||
        !attr(&r, IFLA_CAN_BITTIMING, &bt, sizeof(bt)) ||
        !attr(&r, IFLA_CAN_DATA_BITTIMING, &dbt, sizeof(dbt))) return 1;
    nest_end(&r, data); nest_end(&r, info);
    sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) { perror("socket"); return 1; }
    if (sendto(sock, &r, r.header.nlmsg_len, 0, (struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
        perror("netlink send"); goto out;
    }
    len = recv(sock, buf, sizeof(buf), 0);
    if (len < 0) { perror("netlink recv"); goto out; }
    for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
        if (h->nlmsg_type == NLMSG_ERROR && h->nlmsg_seq == 1 &&
            h->nlmsg_len >= NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
            struct nlmsgerr *err = NLMSG_DATA(h);
            if (err->error) { errno = -err->error; perror("CAN netlink"); break; }
            if (link_state(argv[1], 1) < 0) { perror("interface up"); break; }
            printf("CAN_HW_LOOPBACK_CONFIG_PASS %s nominal=%u data=%u\n",argv[1],bt.bitrate,dbt.bitrate);
            rc = 0; break;
        }
    }
out:
    close(sock);
    return rc;
}
