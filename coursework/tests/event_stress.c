#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", what, errno); exit(1); }
}

static volatile sig_atomic_t interrupted;
static void handle_signal(int signal_number) { (void)signal_number; interrupted = 1; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(90);
    int ep = epoll_create1(0);
    check(ep >= 0, "create epoll");
    for (unsigned i = 0; i < 512; i++) {
        int fd = eventfd(1, EFD_NONBLOCK);
        check(fd >= 0, "create reused fd");
        struct epoll_event event = {.events = EPOLLIN, .data.u64 = i};
        check(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0, "add");
        event.data.u64 = i + 1000;
        check(epoll_ctl(ep, EPOLL_CTL_MOD, fd, &event) == 0, "modify");
        struct epoll_event out[4];
        check(epoll_wait(ep, out, 4, 0) == 1 && out[0].data.u64 == i + 1000, "updated identity");
        if (i % 2 == 0) check(epoll_ctl(ep, EPOLL_CTL_DEL, fd, NULL) == 0, "delete");
        close(fd);
        check(epoll_wait(ep, out, 4, 0) == 0, "closed fd has no stale events");
    }
    close(ep);
    puts("CHURN iterations=512 PASS");
    int fd = eventfd(4096, EFD_SEMAPHORE | EFD_NONBLOCK), results[2], gate[2];
    check(fd >= 0 && pipe(results) == 0 && pipe(gate) == 0, "consumer setup");
    pid_t children[4];
    for (int i = 0; i < 4; i++) {
        children[i] = fork();
        check(children[i] >= 0, "fork consumer");
        if (!children[i]) {
            close(results[0]); close(gate[1]);
            char go;
            check(read(gate[0], &go, 1) == 1, "consumer gate");
            unsigned count = 0;
            uint64_t token;
            ssize_t n;
            while ((n = read(fd, &token, sizeof token)) == sizeof token) {
                check(token == 1, "one token per consumption");
                count++;
                if (count % 16 == 0) sched_yield();
            }
            check(n == -1 && errno == EAGAIN, "consumer exhaustion");
            check(write(results[1], &count, sizeof count) == sizeof count, "consumer report");
            _exit(0);
        }
    }
    close(gate[0]); close(results[1]);
    check(write(gate[1], "GGGG", 4) == 4, "release consumers");
    close(gate[1]);
    unsigned total = 0;
    for (int i = 0; i < 4; i++) {
        unsigned count;
        check(read(results[0], &count, sizeof count) == sizeof count, "read report");
        total += count;
    }
    for (int i = 0; i < 4; i++) {
        int status;
        check(waitpid(children[i], &status, 0) == children[i] && WIFEXITED(status) && WEXITSTATUS(status) == 0, "consumer exit");
    }
    check(total == 4096, "no lost or duplicated tokens");
    close(results[0]); close(fd);
    printf("CONSUMERS workers=4 tokens=%u PASS\n", total);
    fd = eventfd(0, 0);
    check(fd >= 0, "interruptible counter");
    struct sigaction action = {.sa_handler = handle_signal};
    sigemptyset(&action.sa_mask);
    check(sigaction(SIGUSR1, &action, NULL) == 0, "signal handler without restart");
    pid_t parent = getpid(), child = fork();
    check(child >= 0, "signal producer");
    if (!child) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
        nanosleep(&delay, NULL);
        check(kill(parent, SIGUSR1) == 0, "interrupt reader");
        nanosleep(&delay, NULL);
        uint64_t token = 1;
        check(write(fd, &token, sizeof token) == sizeof token, "post-interrupt write");
        _exit(0);
    }
    uint64_t token;
    check(read(fd, &token, sizeof token) == -1 && errno == EINTR && interrupted, "blocking read interrupted");
    check(read(fd, &token, sizeof token) == sizeof token && token == 1, "read after signal");
    int status;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "signal producer exit");
    close(fd);
    puts("SIGNAL interrupted_read_and_retry PASS");
    puts("VERIFY: PASS");
    return 0;
}
