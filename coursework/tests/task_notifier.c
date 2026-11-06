#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", what, errno); exit(1); }
}

static void batch(int semaphore) {
    int fd = eventfd(0, EFD_NONBLOCK | (semaphore ? EFD_SEMAPHORE : 0));
    int ep = epoll_create1(0), gate[2];
    check(fd >= 0 && ep >= 0 && pipe(gate) == 0, "setup");
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = 42};
    check(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0, "epoll add");
    pid_t child = fork();
    check(child >= 0, "fork");
    if (child == 0) {
        char go;
        close(gate[1]);
        if (read(gate[0], &go, 1) != 1 || go != 'G') _exit(2);
        uint64_t count = 3;
        _exit(write(fd, &count, sizeof count) == sizeof count ? 0 : 3);
    }
    close(gate[0]);
    check(write(gate[1], "G", 1) == 1, "start producer");
    close(gate[1]);
    int ready = epoll_wait(ep, &event, 1, 5000);
    check(ready == 1 && (event.events & EPOLLIN) && event.data.u64 == 42, "epoll wait");
    printf("batch=%s submitted=3 epoll=readable\n", semaphore ? "semaphore" : "normal");
    uint64_t total = 0;
    for (int i = 0; i < (semaphore ? 3 : 1); i++) {
        uint64_t value = 0;
        check(read(fd, &value, sizeof value) == sizeof value, "eventfd read");
        printf("read=%llu\n", (unsigned long long)value);
        check(value == (semaphore ? 1u : 3u), "read value");
        total += value;
    }
    uint64_t value;
    errno = 0;
    check(read(fd, &value, sizeof value) == -1 && errno == EAGAIN, "drained EAGAIN");
    int status;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "producer exit");
    check(total == 3, "consumed total");
    printf("consumed=%llu drained=EAGAIN child=0\n", (unsigned long long)total);
    close(fd); close(ep);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    batch(1);
    batch(0);
    puts("VERIFY: PASS");
    return 0;
}
