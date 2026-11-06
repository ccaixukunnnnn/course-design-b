#define _GNU_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { WARMUP = 32, SAMPLES = 256 };
static void check(int ok) { if (!ok) { perror("notification benchmark"); exit(1); } }
static uint64_t ns(void) {
    struct timespec ts;
    check(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

static void measure(int semaphore, unsigned batch) {
    int request = eventfd(0, semaphore ? EFD_SEMAPHORE : 0);
    int ack = eventfd(0, 0), ep = epoll_create1(0);
    check(request >= 0 && ack >= 0 && ep >= 0);
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = 1};
    check(epoll_ctl(ep, EPOLL_CTL_ADD, request, &event) == 0);
    pid_t child = fork();
    check(child >= 0);
    if (!child) {
        for (int i = 0; i < WARMUP + SAMPLES; i++) {
            check(epoll_wait(ep, &event, 1, 5000) == 1);
            uint64_t total = 0, value;
            while (total < batch) {
                check(read(request, &value, sizeof value) == sizeof value);
                check(value == (semaphore ? 1u : batch));
                total += value;
            }
            check(write(ack, &total, sizeof total) == sizeof total);
        }
        _exit(0);
    }
    uint64_t samples[SAMPLES];
    for (int i = -WARMUP; i < SAMPLES; i++) {
        uint64_t value = batch;
        uint64_t start = ns();
        check(write(request, &value, sizeof value) == sizeof value);
        check(read(ack, &value, sizeof value) == sizeof value && value == batch);
        uint64_t elapsed = ns() - start;
        if (i >= 0) samples[i] = elapsed;
    }
    int status;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(request); close(ack); close(ep);
    for (int i = 0; i < SAMPLES; i++)
        printf("RTT mode=%s batch=%u sample=%d ns=%" PRIu64 "\n",
               semaphore ? "semaphore" : "normal", batch, i, samples[i]);
}

int main(void) {
    alarm(90);
    measure(0, 1); measure(1, 1);
    measure(0, 16); measure(1, 16);
    puts("VERIFY: PASS");
    return 0;
}
