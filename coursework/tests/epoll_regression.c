#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int ep = epoll_create1(0);
    int fd = eventfd(1, EFD_NONBLOCK);
    if (ep < 0 || fd < 0) return 2;
    struct epoll_event interest = {.events = EPOLLIN, .data.u64 = 10};
    struct epoll_event events[8];
    check(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &interest) == 0, "add ready fd");
    int n = epoll_wait(ep, events, 8, 0);
    check(n == 1 && events[0].data.u64 == 10, "one result per registration per wait");
    interest.data.u64 = 20;
    check(epoll_ctl(ep, EPOLL_CTL_MOD, fd, &interest) == 0, "modify queued registration");
    n = epoll_wait(ep, events, 8, 0);
    check(n == 1 && events[0].data.u64 == 20, "MOD preserves readiness with new data");
    interest.events = EPOLLIN | EPOLLONESHOT;
    check(epoll_ctl(ep, EPOLL_CTL_MOD, fd, &interest) == 0, "arm oneshot while ready");
    check(epoll_wait(ep, events, 8, 0) == 1, "oneshot first event");
    check(epoll_wait(ep, events, 8, 0) == 0, "oneshot disabled after delivery");
    check(epoll_ctl(ep, EPOLL_CTL_MOD, fd, &interest) == 0, "rearm oneshot");
    check(epoll_wait(ep, events, 8, 0) == 1, "oneshot event after rearm");
    uint64_t value;
    check(read(fd, &value, sizeof value) == sizeof value && value == 1, "drain counter");
    interest.events = EPOLLIN | EPOLLET;
    check(epoll_ctl(ep, EPOLL_CTL_MOD, fd, &interest) == 0, "switch to edge trigger");
    check(epoll_wait(ep, events, 8, 0) == 0, "empty edge source");
    value = 1;
    check(write(fd, &value, sizeof value) == sizeof value, "edge write");
    check(epoll_wait(ep, events, 8, 0) == 1, "edge delivered");
    check(epoll_wait(ep, events, 8, 0) == 0, "edge not repeated without write");
    check(read(fd, &value, sizeof value) == sizeof value, "drain edge source");
    check(write(fd, &value, sizeof value) == sizeof value, "second edge write");
    check(epoll_wait(ep, events, 8, 0) == 1, "second edge delivered");
    check(epoll_ctl(ep, EPOLL_CTL_DEL, fd, NULL) == 0, "delete registration");
    check(epoll_wait(ep, events, 8, 0) == 0, "deleted source not delivered");
    close(fd);
    close(ep);

    ep = epoll_create1(0);
    int sources[3];
    for (int i = 0; i < 3; i++) {
        sources[i] = eventfd(1, EFD_NONBLOCK);
        interest.events = EPOLLIN;
        interest.data.u64 = (uint64_t)i;
        if (sources[i] < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, sources[i], &interest)) return 2;
    }
    unsigned seen = 0;
    for (int i = 0; i < 3; i++) {
        n = epoll_wait(ep, events, 1, 0);
        check(n == 1 && events[0].data.u64 < 3, "bounded result buffer");
        if (n == 1 && events[0].data.u64 < 3) seen |= 1u << events[0].data.u64;
    }
    check(seen == 7, "round robin visits all ready sources");
    n = epoll_wait(ep, events, 8, 0);
    check(n == 3, "large result buffer has three distinct registrations");
    seen = 0;
    for (int i = 0; i < n; i++) {
        if (events[i].data.u64 < 3) seen |= 1u << events[i].data.u64;
    }
    check(seen == 7, "all ready data values preserved");
    for (int i = 0; i < 3; i++) close(sources[i]);
    close(ep);
    if (failures) return 1;
    puts("VERIFY: PASS");
    return 0;
}
