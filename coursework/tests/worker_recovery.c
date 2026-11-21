#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

enum { WORKERS = 2, FIRST_JOB = 42, NO_JOB = -1, EXIT_INJECTED = 7, RESTART_LIMIT = 4 };
enum { HEARTBEAT_EVENT = 0 };
struct job { unsigned id, attempt; };
struct result { struct job job; unsigned value; };
struct worker { pid_t pid; int commands, results, pending; };

static void require(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", what, errno); exit(1); }
}

static void run_worker(int commands, int results, bool exit_after_result) {
    struct job job;
    ssize_t size;
    while ((size = read(commands, &job, sizeof job)) != 0) {
        require(size == sizeof job, "read assigned job");
        bool injected_exit = job.id == FIRST_JOB && job.attempt == 1;
        if (injected_exit && !exit_after_result) _exit(EXIT_INJECTED);
        const struct result answer = {job, job.id * job.id};
        require(write(results, &answer, sizeof answer) == sizeof answer, "write complete result");
        if (injected_exit) _exit(EXIT_INJECTED);
    }
    _exit(0);
}

static void start_worker(struct worker pool[WORKERS], unsigned slot, int ep, int heartbeat,
                         bool exit_after_result) {
    int commands[2], results[2];
    require(pipe(commands) == 0 && pipe(results) == 0, "create private worker pipes");
    require(fcntl(results[0], F_SETFL, O_NONBLOCK) == 0, "make result reader nonblocking");
    pid_t pid = fork();
    require(pid >= 0, "start worker process");
    if (pid == 0) {
        close(ep); close(heartbeat); close(commands[1]); close(results[0]);
        for (unsigned i = 0; i < WORKERS; i++) {
            if (pool[i].pid > 0) {
                close(pool[i].commands);
                close(pool[i].results);
            }
        }
        run_worker(commands[0], results[1], exit_after_result);
    }
    close(commands[0]); close(results[1]);
    pool[slot] = (struct worker){pid, commands[1], results[0], NO_JOB};
    struct epoll_event event = {.events = EPOLLIN | EPOLLHUP, .data.u32 = slot + 1};
    require(epoll_ctl(ep, EPOLL_CTL_ADD, results[0], &event) == 0, "watch worker results");
}

static void assign(struct worker *worker, const struct job jobs[], int index) {
    worker->pending = index;
    ssize_t size = write(worker->commands, &jobs[index], sizeof jobs[index]);
    // The worker may exit just before assignment. Keep the pending job so
    // the supervisor can retry it after waitpid confirms the exit.
    require(size == sizeof jobs[index] || (size == -1 && errno == EPIPE), "send job to worker");
}

static bool collect(struct worker *worker, const struct job jobs[]) {
    if (worker->pending == NO_JOB) return false;
    struct result result;
    ssize_t size = read(worker->results, &result, sizeof result);
    if (size == 0 || (size == -1 && errno == EAGAIN)) return false;
    const struct job expected = jobs[worker->pending];
    require(size == sizeof result && result.job.id == expected.id &&
            result.job.attempt == expected.attempt && result.value == expected.id * expected.id,
            "validate current job result");
    printf("COMMIT job=%u attempt=%u value=%u\n", expected.id, expected.attempt, result.value);
    worker->pending = NO_JOB;
    return true;
}

static void run(bool exit_after_result) {
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int heartbeat = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    require(ep >= 0 && heartbeat >= 0, "initialize worker supervisor");
    const struct itimerspec tick = {.it_interval = {0, 20000000}, .it_value = {0, 20000000}};
    require(timerfd_settime(heartbeat, 0, &tick, NULL) == 0, "arm supervisor heartbeat");
    struct epoll_event event = {.events = EPOLLIN, .data.u32 = HEARTBEAT_EVENT};
    require(epoll_ctl(ep, EPOLL_CTL_ADD, heartbeat, &event) == 0, "watch supervisor heartbeat");

    struct worker pool[WORKERS] = {0};
    struct job jobs[] = {{FIRST_JOB, 1}, {FIRST_JOB + 1, 1}, {FIRST_JOB + 2, 1}};
    int total = exit_after_result ? 2 : 3;
    for (unsigned slot = 0; slot < WORKERS; slot++) {
        start_worker(pool, slot, ep, heartbeat, exit_after_result);
        assign(&pool[slot], jobs, (int)slot);
    }

    int next = WORKERS, committed = 0, restarted = 0, requeued = 0, injected_exits = 0;
    while (committed < total || restarted == 0) {
        struct epoll_event ready[WORKERS + 1];
        int count = epoll_wait(ep, ready, WORKERS + 1, 2000);
        require(count > 0, "supervisor receives events");
        for (int i = 0; i < count; i++) {
            if (ready[i].data.u32 == HEARTBEAT_EVENT) {
                uint64_t ticks;
                require(read(heartbeat, &ticks, sizeof ticks) == sizeof ticks, "consume supervisor heartbeat");
            }
        }
        for (unsigned slot = 0; slot < WORKERS; slot++) {
            struct worker *worker = &pool[slot];
            int status;
            pid_t ended = waitpid(worker->pid, &status, WNOHANG);
            require(ended == 0 || ended == worker->pid, "check worker exit");

            // A result written before exit remains valid in the old pipe.
            if (collect(worker, jobs)) committed++;
            if (ended > 0) {
                require(restarted < RESTART_LIMIT, "restart limit reached");
                if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_INJECTED) injected_exits++;
                int lost = worker->pending;
                require(epoll_ctl(ep, EPOLL_CTL_DEL, worker->results, NULL) == 0, "remove exited worker from epoll");
                close(worker->commands); close(worker->results);
                worker->pid = 0;
                start_worker(pool, slot, ep, heartbeat, exit_after_result);
                restarted++;
                printf("RESTART slot=%u old_pid=%ld new_pid=%ld %s=%d\n", slot, (long)ended,
                       (long)worker->pid, WIFEXITED(status) ? "exit" : "signal",
                       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
                if (lost != NO_JOB) {
                    jobs[lost].attempt++;
                    requeued++;
                    printf("REQUEUE job=%u attempt=%u\n", jobs[lost].id, jobs[lost].attempt);
                    assign(worker, jobs, lost);
                }
            }
            if (worker->pending == NO_JOB && next < total) assign(worker, jobs, next++);
        }
    }

    require(committed == total && restarted == 1 && injected_exits == 1 && requeued == (exit_after_result ? 0 : 1),
            "verify work preservation and restart counts");
    for (unsigned slot = 0; slot < WORKERS; slot++) close(pool[slot].commands);
    for (unsigned slot = 0; slot < WORKERS; slot++) {
        int status;
        require(waitpid(pool[slot].pid, &status, 0) == pool[slot].pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "shut down and reap active workers");
        close(pool[slot].results);
    }
    close(heartbeat); close(ep);
    printf("%s committed=%d restarted=%d requeued=%d PASS\n",
           exit_after_result ? "EXIT_AFTER_RESULT" : "EXIT_BEFORE_RESULT", committed, restarted, requeued);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(15);
    require(signal(SIGPIPE, SIG_IGN) != SIG_ERR, "handle assignment after peer exit");
    run(false);
    run(true);
    puts("VERIFY: PASS");
    return 0;
}
