#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static void require(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", what, errno); exit(1); }
}

static struct itimerspec after(long milliseconds) {
    return (struct itimerspec){.it_value = {milliseconds / 1000, milliseconds % 1000 * 1000000}};
}

static uint64_t now_ns(void) {
    struct timespec now;
    require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "clock_gettime");
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}

static void consume(int timer) {
    uint64_t ticks;
    require(read(timer, &ticks, sizeof ticks) == sizeof ticks && ticks == 1, "one expiration consumed");
}

int main(void) {
    alarm(12);
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    require(timer >= 0 && ep >= 0, "create timer and epoll");
    int fd_flags = fcntl(timer, F_GETFD), status_flags = fcntl(timer, F_GETFL);
    require(fd_flags >= 0 && (fd_flags & FD_CLOEXEC), "close-on-exec flag");
    require(status_flags >= 0 && (status_flags & O_NONBLOCK), "nonblocking flag");
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = 42};
    require(epoll_ctl(ep, EPOLL_CTL_ADD, timer, &event) == 0, "register timer");

    uint64_t ticks;
    require(read(timer, &ticks, sizeof ticks) == -1 && errno == EAGAIN, "fresh timer is empty");
    require(read(timer, &ticks, 7) == -1 && errno == EINVAL, "short expiration buffer");
    struct itimerspec invalid = {.it_value = {0, 1000000000}};
    require(timerfd_settime(timer, 0, &invalid, NULL) == -1 && errno == EINVAL, "reject invalid nanoseconds");

    struct itimerspec setting = after(4000), remaining;
    require(timerfd_settime(timer, 0, &setting, NULL) == 0, "arm long timer");
    require(timerfd_gettime(timer, &remaining) == 0 && remaining.it_value.tv_sec > 0,
            "report an armed timer");
    uint64_t deadline = now_ns() + 35000000;
    setting.it_value = (struct timespec){deadline / 1000000000, deadline % 1000000000};
    require(timerfd_settime(timer, TFD_TIMER_ABSTIME, &setting, &remaining) == 0 &&
            remaining.it_value.tv_sec > 0, "replace long timer and report previous time");
    require(epoll_wait(ep, &event, 1, 2000) == 1 && event.data.u64 == 42 && (event.events & EPOLLIN),
            "earlier rearm wakes epoll");
    require(now_ns() >= deadline, "no premature notification");
    consume(timer);
    require(read(timer, &ticks, sizeof ticks) == -1 && errno == EAGAIN, "one shot stays drained");
    puts("PASS earlier absolute rearm and one-shot epoll delivery");

    setting = after(25);
    require(timerfd_settime(timer, 0, &setting, NULL) == 0, "arm before disarming");
    setting = after(0);
    require(timerfd_settime(timer, 0, &setting, NULL) == 0, "disarm timer");
    require(epoll_wait(ep, &event, 1, 70) == 0, "cancelled deadline stays silent");
    require(timerfd_gettime(timer, &remaining) == 0 && remaining.it_value.tv_sec == 0 &&
            remaining.it_value.tv_nsec == 0, "report a disarmed timer");

    setting = (struct itimerspec){.it_value = {0, 1}};
    require(timerfd_settime(timer, TFD_TIMER_ABSTIME, &setting, NULL) == 0, "arm already expired deadline");
    require(epoll_wait(ep, &event, 1, 1000) == 1, "already expired timer wakes epoll");
    setting = after(0);
    require(timerfd_settime(timer, 0, &setting, NULL) == 0, "discard pending expiration");
    require(read(timer, &ticks, sizeof ticks) == -1 && errno == EAGAIN, "pending expiry cleared");
    puts("PASS cancellation and clearing unread expirations");

    int blocking = timerfd_create(CLOCK_MONOTONIC, 0);
    setting = after(15);
    require(blocking >= 0 && timerfd_settime(blocking, 0, &setting, NULL) == 0, "arm blocking timer");
    consume(blocking);
    close(blocking);
    int other = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    event = (struct epoll_event){.events = EPOLLIN, .data.u64 = 43};
    require(other >= 0 && epoll_ctl(ep, EPOLL_CTL_ADD, other, &event) == 0, "register second timer");
    setting = after(4000);
    require(timerfd_settime(timer, 0, &setting, NULL) == 0, "arm timer before close");
    setting = after(25);
    require(timerfd_settime(other, 0, &setting, NULL) == 0, "arm an earlier timer on a different fd");
    close(timer);
    require(epoll_wait(ep, &event, 1, 2000) == 1 && event.data.u64 == 43, "only the live timer expires");
    consume(other);
    close(other);
    close(ep);
    puts("PASS blocking read and close with a pending deadline");
    puts("VERIFY: PASS");
    return 0;
}
