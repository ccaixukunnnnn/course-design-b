#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

enum { TIMED = 42, ORDINARY_A = 43, ORDINARY_B = 44, WORKERS = 2 };
enum { COMPLETION = 1, DEADLINE = 2 };

struct task { unsigned id, attempt; };
struct result { struct task task; unsigned value; };

static void require(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", what, errno); exit(1); }
}

static void submit(int fd, unsigned id, unsigned attempt) {
    const struct task task = {id, attempt};
    require(write(fd, &task, sizeof task) == sizeof task, "submit task");
}

static void worker(int command, int result, int notification, int gate, bool stall) {
    struct task task;
    ssize_t size;
    while ((size = read(command, &task, sizeof task)) != 0) {
        require(size == sizeof task, "read worker command");
        if (stall && task.id == TIMED && task.attempt == 1) {
            char release;
            require(read(gate, &release, 1) == 1, "release the simulated stalled worker");
        }
        const struct result completed = {task, task.id * task.id};
        const uint64_t one = 1;
        require(write(result, &completed, sizeof completed) == sizeof completed, "publish result record");
        require(write(notification, &one, sizeof one) == sizeof one, "publish completion event");
    }
    _exit(0);
}

static void run(bool simultaneous) {
    int commands[WORKERS][2], results[2], gate[2];
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int done = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    require(ep >= 0 && done >= 0 && timer >= 0, "initialize event sources");
    require(pipe(results) == 0 && pipe(gate) == 0, "create result and stall pipes");
    struct epoll_event event = {.events = EPOLLIN, .data.u32 = COMPLETION};
    require(epoll_ctl(ep, EPOLL_CTL_ADD, done, &event) == 0, "register completion eventfd");
    event.data.u32 = DEADLINE;
    require(epoll_ctl(ep, EPOLL_CTL_ADD, timer, &event) == 0, "register deadline timerfd");
    pid_t children[WORKERS];
    for (int i = 0; i < WORKERS; i++) require(pipe(commands[i]) == 0, "create worker pipe");
    for (int i = 0; i < WORKERS; i++) {
        children[i] = fork();
        require(children[i] >= 0, "fork worker");
        if (children[i] == 0) {
            close(ep); close(timer); close(results[0]); close(gate[1]);
            for (int j = 0; j < WORKERS; j++) {
                close(commands[j][1]);
                if (i != j) close(commands[j][0]);
            }
            worker(commands[i][0], results[1], done, gate[0], i == 0 && !simultaneous);
        }
    }
    close(gate[0]); close(results[1]);
    for (int i = 0; i < WORKERS; i++) close(commands[i][0]);
    unsigned attempt = 1, accepted = 0, ignored = 0, retries = 0;
    bool seen[3] = {false};
    const unsigned goal = simultaneous ? 1 : 3;
    const struct itimerspec disarmed = {0};
    struct itimerspec deadline = {.it_value = {0, 100000000}};

    submit(commands[0][1], TIMED, attempt);
    if (simultaneous) {
        struct pollfd pending = {.fd = done, .events = POLLIN};
        require(poll(&pending, 1, 2000) == 1 && (pending.revents & POLLIN), "completion becomes ready");
        deadline.it_value.tv_nsec = 1;
        require(timerfd_settime(timer, TFD_TIMER_ABSTIME, &deadline, NULL) == 0, "arm an expired deadline");
        pending.fd = timer;
        require(poll(&pending, 1, 2000) == 1 && (pending.revents & POLLIN), "deadline also becomes ready");
    } else {
        require(timerfd_settime(timer, 0, &deadline, NULL) == 0, "set task deadline");
        submit(commands[1][1], ORDINARY_A, 1);
        submit(commands[1][1], ORDINARY_B, 1);
    }

    while (accepted < goal || (!simultaneous && ignored == 0)) {
        struct epoll_event events[2];
        int ready = epoll_wait(ep, events, 2, 2000);
        require(ready > 0, "progress before watchdog");
        bool completion_ready = false, deadline_ready = false;
        for (int i = 0; i < ready; i++) {
            completion_ready |= events[i].data.u32 == COMPLETION;
            deadline_ready |= events[i].data.u32 == DEADLINE;
        }
        if (simultaneous) require(completion_ready && deadline_ready, "both events returned in one batch");

        // A completion in this epoll batch wins before any retry is issued.
        if (completion_ready) {
            uint64_t count;
            require(read(done, &count, sizeof count) == sizeof count && count <= 4, "read completion count");
            while (count-- > 0) {
                struct result result;
                require(read(results[0], &result, sizeof result) == sizeof result, "read result record");
                unsigned id = result.task.id;
                require(id >= TIMED && id <= ORDINARY_B && result.value == id * id, "validate result payload");
                if (id == TIMED && result.task.attempt < attempt) {
                    ignored++;
                    printf("STALE task=%u attempt=%u ignored\n", id, result.task.attempt);
                    continue;
                }
                require(!seen[id - TIMED] && result.task.attempt == (id == TIMED ? attempt : 1),
                        "commit one result for the current attempt");
                seen[id - TIMED] = true;
                accepted++;
                printf("COMMIT task=%u attempt=%u value=%u\n", id, result.task.attempt, result.value);
                if (id == TIMED) require(timerfd_settime(timer, 0, &disarmed, NULL) == 0, "cancel completed deadline");
            }
        }
        if (deadline_ready && !seen[0]) {
            uint64_t count;
            require(read(timer, &count, sizeof count) == sizeof count && count == 1 && retries == 0,
                    "consume one deadline event");
            retries++;
            attempt++;
            printf("TIMEOUT task=%u attempt=1, requeue attempt=%u\n", TIMED, attempt);
            submit(commands[1][1], TIMED, attempt);
            require(write(gate[1], "g", 1) == 1, "allow the outdated attempt to return late");
        }
    }
    require(seen[0] && accepted == goal && retries == (simultaneous ? 0u : 1u) &&
            ignored == (simultaneous ? 0u : 1u), "verify recovery and commit counts");
    for (int i = 0; i < WORKERS; i++) close(commands[i][1]);
    close(gate[1]); close(results[0]); close(ep); close(timer); close(done);
    for (int i = 0; i < WORKERS; i++) {
        int status;
        require(waitpid(children[i], &status, 0) == children[i] && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "reap worker");
    }
    printf("%s committed=%u requeued=%u stale=%u PASS\n",
           simultaneous ? "COMPLETION_AND_TIMEOUT" : "TIMEOUT_RECOVERY", accepted, retries, ignored);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(15);
    run(false);
    run(true);
    puts("VERIFY: PASS");
    return 0;
}
