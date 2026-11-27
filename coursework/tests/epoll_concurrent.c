#define _GNU_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

enum { ROUNDS = 64, WAIT_MS = 2000 };

static void require(int condition, const char *what, unsigned round) {
    if (!condition) {
        fprintf(stderr, "FAIL round %u: %s\n", round, what);
        exit(1);
    }
}

static void expect_event(int ep, uint64_t tag, unsigned round) {
    struct epoll_event events[4];
    int count = epoll_wait(ep, events, 4, WAIT_MS);
    require(count == 1 && events[0].events == EPOLLIN && events[0].data.u64 == tag,
            "expected one EPOLLIN event with the current MOD tag", round);
    int timeout = round == 1 ? 10 : 0;
    require(epoll_wait(ep, events, 4, timeout) == 0,
            "ONESHOT stays disabled while the counter is still readable", round);
}

int main(void) {
    alarm(20);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int counter = eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);
    int start[2], entered[2];
    require(ep >= 0 && counter >= 0 && pipe(start) == 0 && pipe(entered) == 0, "setup", 0);
    const uint64_t tag_base = UINT64_C(0xaced000000000000);
    struct epoll_event interest = {.events = EPOLLIN | EPOLLONESHOT, .data.u64 = tag_base};
    require(epoll_ctl(ep, EPOLL_CTL_ADD, counter, &interest) == 0, "initial ADD", 0);
    expect_event(ep, tag_base, 0);
    uint64_t value;
    require(read(counter, &value, sizeof value) == sizeof value && value == 1, "initial drain", 0);

    pid_t child = fork();
    require(child >= 0, "fork waiter", 0);
    if (child == 0) {
        close(start[1]);
        close(entered[0]);
        for (unsigned round = 1; round <= ROUNDS; round++) {
            char token;
            require(read(start[0], &token, 1) == 1, "receive round start", round);
            struct epoll_event event;
            require(epoll_wait(ep, &event, 1, 0) == 0, "no event before rearm", round);
            // The modifier starts after this checkpoint. Either syscall can
            // enter the kernel first, and both orders must deliver the event.
            require(write(entered[1], "w", 1) == 1, "signal wait checkpoint", round);
            expect_event(ep, tag_base + round, round);
            require(read(counter, &value, sizeof value) == sizeof value && value == 1,
                    "drain exactly one token", round);
            require(write(entered[1], "d", 1) == 1, "acknowledge delivery and drain", round);
        }
        _exit(0);
    }

    close(start[0]);
    close(entered[1]);
    const uint64_t one = 1;
    for (unsigned round = 1; round <= ROUNDS; round++) {
        int already_ready = round % 2 != 0;
        if (already_ready) {
            require(write(counter, &one, sizeof one) == sizeof one, "write while disabled", round);
        }
        char token;
        require(write(start[1], "s", 1) == 1, "start waiter", round);
        require(read(entered[0], &token, 1) == 1 && token == 'w', "wait checkpoint", round);
        interest.data.u64 = tag_base + round;
        require(epoll_ctl(ep, EPOLL_CTL_MOD, counter, &interest) == 0, "rearm ONESHOT", round);
        if (!already_ready) {
            require(write(counter, &one, sizeof one) == sizeof one, "write after rearm", round);
        }
        require(read(entered[0], &token, 1) == 1 && token == 'd', "wait for delivery", round);
    }
    int status;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "waiter exit", ROUNDS);
    close(start[1]);
    close(entered[0]);
    close(counter);
    close(ep);
    printf("PASS %d shared epoll ONESHOT MOD/wait rounds\n", ROUNDS);
    puts("VERIFY: PASS");
    return 0;
}
