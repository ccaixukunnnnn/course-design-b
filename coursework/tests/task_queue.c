#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

enum { PRODUCERS = 4, TASKS = 256, BATCH = 8 };
struct task { uint32_t id, value; };

static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", message, errno); exit(1); }
}

static void transfer(int fd, void *buf, size_t size, int writing) {
    size_t done = 0;
    while (done < size) {
        ssize_t n = writing ? write(fd, (char *)buf + done, size - done)
                            : read(fd, (char *)buf + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        check(n > 0, "complete task transfer");
        done += (size_t)n;
    }
}

static void run(int semaphore, int failing_producer) {
    int pipes[PRODUCERS][2], notify[PRODUCERS];
    pid_t children[PRODUCERS];
    int ep = epoll_create1(0);
    check(ep >= 0, "epoll create");
    for (int p = 0; p < PRODUCERS; p++) {
        check(pipe(pipes[p]) == 0, "task pipe");
        notify[p] = eventfd(0, EFD_NONBLOCK | (semaphore ? EFD_SEMAPHORE : 0));
        check(notify[p] >= 0, "notification counter");
        struct epoll_event event = {.events = EPOLLIN, .data.u32 = (unsigned)p};
        check(epoll_ctl(ep, EPOLL_CTL_ADD, notify[p], &event) == 0, "register producer");
    }
    for (int p = 0; p < PRODUCERS; p++) {
        children[p] = fork();
        check(children[p] >= 0, "fork producer");
        if (children[p] == 0) {
            close(ep);
            for (int q = 0; q < PRODUCERS; q++) {
                close(pipes[q][0]);
                if (q != p) { close(pipes[q][1]); close(notify[q]); }
            }
            int limit = p == failing_producer ? TASKS / 2 : TASKS;
            for (int first = 0; first < limit; first += BATCH) {
                for (int j = 0; j < BATCH; j++) {
                    struct task task = {(uint32_t)(p * TASKS + first + j), (uint32_t)(first + j + 1)};
                    transfer(pipes[p][1], &task, sizeof task, 1);
                }
                uint64_t count = BATCH;
                transfer(notify[p], &count, sizeof count, 1);
            }
            _exit(p == failing_producer ? 7 : 0);
        }
    }
    for (int p = 0; p < PRODUCERS; p++) close(pipes[p][1]);
    unsigned char seen[PRODUCERS * TASKS] = {0};
    unsigned received = 0;
    unsigned expected = PRODUCERS * TASKS - (failing_producer >= 0 ? TASKS / 2 : 0);
    uint64_t sum = 0, expected_sum = 0;
    for (int p = 0; p < PRODUCERS; p++) {
        int limit = p == failing_producer ? TASKS / 2 : TASKS;
        for (int j = 1; j <= limit; j++) expected_sum += (uint64_t)j * j;
    }
    while (received < expected) {
        struct epoll_event events[PRODUCERS * 2];
        int n = epoll_wait(ep, events, PRODUCERS * 2, 5000);
        if (n < 0 && errno == EINTR) continue;
        check(n > 0 && n <= PRODUCERS, "bounded notification wait");
        for (int i = 0; i < n; i++) {
            unsigned p = events[i].data.u32;
            check(p < PRODUCERS && (events[i].events & EPOLLIN), "producer identity");
            uint64_t count;
            transfer(notify[p], &count, sizeof count, 0);
            check(count > 0 && count <= TASKS && (!semaphore || count == 1), "notification count");
            for (uint64_t j = 0; j < count; j++) {
                struct task task;
                transfer(pipes[p][0], &task, sizeof task, 0);
                check(task.id < PRODUCERS * TASKS && task.id / TASKS == p, "task identity");
                check(!seen[task.id], "no duplicate task");
                check(task.value == task.id % TASKS + 1, "task payload");
                seen[task.id] = 1;
                received++;
                sum += (uint64_t)task.value * task.value;
            }
        }
    }
    check(received == expected && sum == expected_sum, "task completion and results");
    for (int p = 0; p < PRODUCERS; p++) {
        int status;
        check(waitpid(children[p], &status, 0) == children[p] && WIFEXITED(status), "reap producer");
        check(WEXITSTATUS(status) == (p == failing_producer ? 7 : 0), "producer exit status");
        struct task extra;
        check(read(pipes[p][0], &extra, sizeof extra) == 0, "no unconsumed task");
        uint64_t count;
        check(read(notify[p], &count, sizeof count) == -1 && errno == EAGAIN, "counter drained");
        close(pipes[p][0]); close(notify[p]);
    }
    close(ep);
    printf("QUEUE mode=%s producers=%d completed=%u checksum=%" PRIu64 " failed_producer=%d PASS\n",
           semaphore ? "semaphore" : "normal", PRODUCERS, received, sum, failing_producer);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(60);
    run(0, -1);
    run(1, -1);
    run(0, 2);
    puts("VERIFY: PASS");
    return 0;
}
