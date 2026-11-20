// SPDX-License-Identifier: MPL-2.0
#include "nbt/firewall.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef __QNX__
#include <fcntl.h>
#include <net/if.h>
#include <net/pfvar.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define REMOVE_MAX 16u /* our rules removed per call; one is normal */

static int pf_device(void) {
    int fd = open("/dev/pf", O_RDWR);
    if (fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC)) {
        close(fd);
        fd = -1;
    }
    return fd;
}

/* A change ticket for the main filter ruleset. Every change made by anyone
 * after this invalidates it, so a stale rule number is never used. */
static int change_ticket(int fd, uint32_t *ticket) {
    struct pfioc_rule pr;
    memset(&pr, 0, sizeof(pr));
    pr.action = PF_CHANGE_GET_TICKET;
    pr.rule.action = PF_PASS; /* selects the filter ruleset */
    if (ioctl(fd, DIOCCHANGERULE, &pr)) {
        return -1;
    }
    *ticket = pr.ticket;
    return 0;
}

/* Removes the first rule of ours (`label` exactly, or any label starting with
 * it when `prefix`): 1 removed, 0 none left, -1 error. The scan runs under the
 * change ticket: if the ruleset changes meanwhile, the read ticket differs or
 * the removal is refused (EINVAL, EBUSY). */
static int remove_one(int fd, const char *label, int prefix) {
    struct pfioc_rule pr;
    uint32_t ticket, count, i;
    if (change_ticket(fd, &ticket)) {
        return -1;
    }
    memset(&pr, 0, sizeof(pr));
    pr.rule.action = PF_PASS;
    if (ioctl(fd, DIOCGETRULES, &pr)) {
        return -1;
    }
    if (pr.ticket != ticket) {
        errno = EBUSY;
        return -1;
    }
    count = pr.nr;
    for (i = 0; i < count; ++i) {
        memset(&pr, 0, sizeof(pr));
        pr.rule.action = PF_PASS;
        pr.ticket = ticket;
        pr.nr = i;
        if (ioctl(fd, DIOCGETRULE, &pr)) {
            return -1;
        }
        if (prefix ? !strncmp(pr.rule.label, label, strlen(label))
                   : !strncmp(pr.rule.label, label, sizeof(pr.rule.label))) {
            memset(&pr, 0, sizeof(pr));
            pr.action = PF_CHANGE_REMOVE;
            pr.ticket = ticket;
            pr.rule.action = PF_PASS;
            pr.nr = i;
            return ioctl(fd, DIOCCHANGERULE, &pr) ? -1 : 1;
        }
    }
    return 0;
}

static int remove_all(int fd, const char *label, int prefix) {
    unsigned removed = 0;
    for (;;) {
        int result = remove_one(fd, label, prefix);
        if (result < 0) {
            return -1;
        }
        if (!result) {
            return (int)removed;
        }
        if (++removed >= REMOVE_MAX) {
            errno = EMLINK;
            return -1;
        }
    }
}

int nbt_firewall_close_label(const char *label) {
    if (!label || (strcmp(label, "nbt-manager") && strcmp(label, "nbt-door"))) {
        errno = EINVAL;
        return -1;
    }
    int fd = pf_device(), result, saved;
    if (fd < 0) {
        return -1;
    }
    result = remove_all(fd, label, 0);
    saved = errno;
    close(fd);
    errno = saved;
    return result;
}

int nbt_firewall_open_label(const char *interface_name, uint32_t address, uint16_t port,
                            const char *label) {
    struct pfioc_pooladdr pool;
    struct pfioc_rule pr;
    struct pf_rule *rule = &pr.rule;
    uint32_t ticket;
    int fd, result = -1, saved;
    if (!label || (strcmp(label, "nbt-manager") && strcmp(label, "nbt-door")) || !interface_name ||
        !*interface_name || strlen(interface_name) >= sizeof(rule->ifname) || !address || !port) {
        errno = EINVAL;
        return -1;
    }
    fd = pf_device();
    if (fd < 0) {
        return -1;
    }
    if (remove_all(fd, label, 0) < 0) {
        goto done;
    }
    /* An empty address pool, required by every rule change. */
    memset(&pool, 0, sizeof(pool));
    if (ioctl(fd, DIOCBEGINADDRS, &pool) || change_ticket(fd, &ticket)) {
        goto done;
    }
    memset(&pr, 0, sizeof(pr));
    pr.action = PF_CHANGE_ADD_HEAD;
    pr.ticket = ticket;
    pr.pool_ticket = pool.ticket;
    rule->action = PF_PASS;
    rule->direction = PF_IN;
    rule->quick = 1;
    rule->af = AF_INET;
    rule->proto = IPPROTO_TCP;
    memcpy(rule->ifname, interface_name, strlen(interface_name) + 1);
    rule->src.addr.type = PF_ADDR_ADDRMASK; /* any: zero address and mask */
    rule->dst.addr.type = PF_ADDR_ADDRMASK;
    rule->dst.addr.v.a.addr.v4.s_addr = address;
    rule->dst.addr.v.a.mask.v4.s_addr = 0xffffffffu;
    rule->dst.port_op = PF_OP_EQ;
    rule->dst.port[0] = htons(port);
    rule->keep_state = PF_STATE_NORMAL;
    rule->flags = TH_SYN;
    rule->flagset = TH_SYN | TH_ACK;
    memcpy(rule->label, label, strlen(label) + 1);
    if (!ioctl(fd, DIOCCHANGERULE, &pr)) {
        result = 0;
    }
done:
    saved = errno;
    close(fd);
    errno = saved;
    return result;
}

/* One rule for an application: any destination address on the interface. */
static int add_app_rule(int fd, const char *interface_name, uint16_t port, const char *label) {
    struct pfioc_pooladdr pool;
    struct pfioc_rule pr;
    struct pf_rule *rule = &pr.rule;
    uint32_t ticket;
    memset(&pool, 0, sizeof(pool));
    if (ioctl(fd, DIOCBEGINADDRS, &pool) || change_ticket(fd, &ticket)) {
        return -1;
    }
    memset(&pr, 0, sizeof(pr));
    pr.action = PF_CHANGE_ADD_HEAD;
    pr.ticket = ticket;
    pr.pool_ticket = pool.ticket;
    rule->action = PF_PASS;
    rule->direction = PF_IN;
    rule->quick = 1;
    rule->af = AF_INET;
    rule->proto = IPPROTO_TCP;
    memcpy(rule->ifname, interface_name, strlen(interface_name) + 1);
    rule->src.addr.type = PF_ADDR_ADDRMASK; /* any: zero address and mask */
    rule->dst.addr.type = PF_ADDR_ADDRMASK;
    rule->dst.port_op = PF_OP_EQ;
    rule->dst.port[0] = htons(port);
    rule->keep_state = PF_STATE_NORMAL;
    rule->flags = TH_SYN;
    rule->flagset = TH_SYN | TH_ACK;
    memcpy(rule->label, label, strlen(label) + 1);
    return ioctl(fd, DIOCCHANGERULE, &pr) ? -1 : 0;
}

static int app_label(const char *app, char *out, size_t size) {
    const int n = app ? snprintf(out, size, "nbt-app-%s", app) : snprintf(out, size, "nbt-app-");
    return n < 0 || (size_t)n >= size || (app && !*app) ? -1 : 0;
}

int nbt_firewall_app_add(const char *app, const char *interface_name, uint16_t port) {
    char label[PF_RULE_LABEL_SIZE];
    int fd, result, saved;
    if (!app || app_label(app, label, sizeof(label)) || !interface_name || !*interface_name ||
        strlen(interface_name) >= IFNAMSIZ || !port) {
        errno = EINVAL;
        return -1;
    }
    fd = pf_device();
    if (fd < 0) {
        return -1;
    }
    result = add_app_rule(fd, interface_name, port, label);
    saved = errno;
    close(fd);
    errno = saved;
    return result;
}

int nbt_firewall_app_close(const char *app) {
    char label[PF_RULE_LABEL_SIZE];
    int fd, result, saved;
    if (app_label(app, label, sizeof(label))) {
        errno = EINVAL;
        return -1;
    }
    fd = pf_device();
    if (fd < 0) {
        return -1;
    }
    result = remove_all(fd, label, app == NULL);
    saved = errno;
    close(fd);
    errno = saved;
    return result;
}

#else

#ifdef NBT_TESTING
#include <stdlib.h>
/* Host tests: record the calls in NBT_TEST_FIREWALL_LOG. */
static void note(const char *line) {
    const char *path = getenv("NBT_TEST_FIREWALL_LOG");
    FILE *log = path ? fopen(path, "a") : NULL;
    if (log) {
        fputs(line, log);
        fputc('\n', log);
        fclose(log);
    }
}
#else
static void note(const char *line) {
    (void)line;
}
#endif

int nbt_firewall_open_label(const char *interface_name, uint32_t address, uint16_t port,
                            const char *label) {
    char line[64];
    const unsigned char *a = (const unsigned char *)&address;
    if (!label || (strcmp(label, "nbt-manager") && strcmp(label, "nbt-door")) || !interface_name ||
        !*interface_name || strlen(interface_name) >= 16 || !address || !port) {
        errno = EINVAL;
        return -1;
    }
    snprintf(line, sizeof(line), "open %s %u.%u.%u.%u %u", interface_name, a[0], a[1], a[2], a[3],
             (unsigned)port);
    note(line);
    return 0;
}

int nbt_firewall_close_label(const char *label) {
    note(label && !strcmp(label, "nbt-door") ? "close door" : "close");
    return 0;
}

int nbt_firewall_app_add(const char *app, const char *interface_name, uint16_t port) {
    char line[96];
    if (!app || !*app || !interface_name || !*interface_name || strlen(interface_name) >= 16 ||
        !port) {
        errno = EINVAL;
        return -1;
    }
    snprintf(line, sizeof(line), "app open %s %s %u", app, interface_name, (unsigned)port);
    note(line);
    return 0;
}

int nbt_firewall_app_close(const char *app) {
    char line[80];
    snprintf(line, sizeof(line), "app close %s", app ? app : "*");
    note(line);
    return 0;
}

#endif

int nbt_firewall_open(const char *interface_name, uint32_t address, uint16_t port) {
    return nbt_firewall_open_label(interface_name, address, port, "nbt-manager");
}
int nbt_firewall_close(void) {
    return nbt_firewall_close_label("nbt-manager");
}
