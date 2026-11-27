#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static const uint64_t NS_PER_SEC = UINT64_C(1000000000);
static const uint64_t PERIOD_NS = UINT64_C(100000000);

static void require(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", what, errno); exit(1); }
}

static struct timespec timespec_from_ns(uint64_t ns) {
    return (struct timespec){ns / NS_PER_SEC, ns % NS_PER_SEC};
}

static uint64_t ns_from_timespec(struct timespec value) {
    return (uint64_t)value.tv_sec * NS_PER_SEC + (uint64_t)value.tv_nsec;
}

static uint64_t now_ns(void) {
    struct timespec time;
    require(clock_gettime(CLOCK_MONOTONIC, &time) == 0, "monotonic clock");
    return ns_from_timespec(time);
}

static uint64_t observe(int timer, uint64_t first, uint64_t previously_read) {
    uint64_t before = now_ns(), ticks;
    require(read(timer, &ticks, sizeof ticks) == sizeof ticks && ticks > 0, "read accumulated ticks");
    uint64_t after = now_ns();
    require(before >= first && ticks + previously_read >= 1 + (before - first) / PERIOD_NS &&
            ticks + previously_read <= 1 + (after - first) / PERIOD_NS, "ticks stay on the original phase");
    return ticks;
}

int main(void) {
    alarm(12);
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    require(timer >= 0 && ep >= 0, "create timer and epoll");
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = 10};
    require(epoll_ctl(ep, EPOLL_CTL_ADD, timer, &event) == 0, "watch timer");

    uint64_t start = now_ns();
    uint64_t first = start + PERIOD_NS;
    struct itimerspec periodic = {.it_interval = timespec_from_ns(PERIOD_NS),
                                  .it_value = timespec_from_ns(first)};
    require(timerfd_settime(timer, TFD_TIMER_ABSTIME, &periodic, NULL) == 0, "arm fixed-phase timer");
    struct timespec workload = timespec_from_ns(550000000);
    require(nanosleep(&workload, NULL) == 0, "simulate 550 ms without consuming ticks");
    struct itimerspec remaining;
    require(timerfd_gettime(timer, &remaining) == 0 &&
            ns_from_timespec(remaining.it_interval) == PERIOD_NS &&
            ns_from_timespec(remaining.it_value) <= PERIOD_NS, "report interval and time to next period");
    require(epoll_wait(ep, &event, 1, 1000) == 1 && event.data.u64 == 10, "wake after missed periods");
    uint64_t first_read = observe(timer, first, 0);
    require(first_read >= 5, "one read counts at least five missed periods");
    printf("SLOW_CONSUMER period_ms=100 pause_ms=550 expirations=%" PRIu64 "\n", first_read);

    require(epoll_wait(ep, &event, 1, 1000) == 1, "periodic timer reawakens after a read");
    uint64_t second_read = observe(timer, first, first_read);
    printf("FIXED_PHASE first_expirations=%" PRIu64 " next_expirations=%" PRIu64 "\n", first_read, second_read);

    struct itimerspec one_shot = {.it_value = timespec_from_ns(1)};
    require(timerfd_settime(timer, TFD_TIMER_ABSTIME, &one_shot, &remaining) == 0 &&
            ns_from_timespec(remaining.it_interval) == PERIOD_NS, "reset periodic timer and report old interval");
    require(epoll_wait(ep, &event, 1, 1000) == 1, "one-shot reset becomes readable");
    uint64_t ticks;
    require(read(timer, &ticks, sizeof ticks) == sizeof ticks && ticks == 1, "reset clears old accumulated ticks");
    require(epoll_wait(ep, &event, 1, 130) == 0, "one-shot reset does not repeat");

    require(timerfd_settime(timer, TFD_TIMER_ABSTIME, &periodic, NULL) == 0, "arm overdue periodic timer");
    require(epoll_wait(ep, &event, 1, 1000) == 1, "overdue periodic timer becomes readable");
    const struct itimerspec disarmed = {0};
    require(timerfd_settime(timer, 0, &disarmed, NULL) == 0, "cancel unread periodic timer");
    require(read(timer, &ticks, sizeof ticks) == -1 && errno == EAGAIN, "cancel discards pending ticks");
    close(ep);
    close(timer);
    puts("VERIFY: PASS");
    return 0;
}
