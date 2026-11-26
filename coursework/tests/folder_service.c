#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum { WORKERS = 2, PENDING_LIMIT = 32, NAME_LIMIT = 96, BURST = 160, FILE_LIMIT = 4 * 1024 * 1024 };
static const uint64_t WATCH_EVENT = 1;
static const uint64_t FNV_SEED = UINT64_C(14695981039346656037);
struct job { char name[NAME_LIMIT]; unsigned attempt; };
struct result { uint64_t hash, bytes; int error; };
struct worker { pid_t pid; int command, result; struct job job; };
struct folders { int inbox, outbox, stage; };
struct service {
    struct folders dirs;
    int ep, notify;
    struct worker workers[WORKERS];
    struct job pending[PENDING_LIMIT];
    unsigned queued, committed, recovered, overflow, scans, event_discovered;
    bool rescan, inject_exit, verbose;
};

static void need(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s (errno=%d)\n", what, errno); exit(1); }
}

static bool job_name(const char *name) {
    size_t len = strlen(name);
    if (len < 5 || len >= NAME_LIMIT || name[0] == '.' || strcmp(name + len - 4, ".job")) return false;
    return strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == len;
}

static void write_all(int fd, const char *data, size_t length) {
    while (length) {
        ssize_t n = write(fd, data, length);
        if (n < 0 && errno == EINTR) continue;
        need(n > 0, "write file");
        data += n;
        length -= (size_t)n;
    }
}

static struct folders open_folders(const char *path) {
    int dir = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    need(dir >= 0, "open service directory");
    const char *names[] = {"inbox", "outbox", "stage"};
    int fds[3];
    for (size_t i = 0; i < 3; i++) {
        need(mkdirat(dir, names[i], 0700) == 0 || errno == EEXIST, "prepare service folders");
        fds[i] = openat(dir, names[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        need(fds[i] >= 0, "open service folder");
    }
    close(dir);
    return (struct folders){fds[0], fds[1], fds[2]};
}

static void close_folders(struct folders dirs) {
    close(dirs.inbox); close(dirs.outbox); close(dirs.stage);
}

static bool exists(int directory, const char *name) {
    struct stat st;
    if (fstatat(directory, name, &st, AT_SYMLINK_NOFOLLOW) == 0) return true;
    need(errno == ENOENT, "look up job name");
    return false;
}

static void put(struct folders dirs, const char *name, const char *payload) {
    need(job_name(name), "use a unique name ending in .job");
    int fd = openat(dirs.stage, name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    need(fd >= 0, "reserve job name and open staging file");
    if (exists(dirs.inbox, name) || exists(dirs.outbox, name)) {
        close(fd);
        unlinkat(dirs.stage, name, 0);
        fprintf(stderr, "FAIL duplicate job name %s\n", name);
        exit(1);
    }
    write_all(fd, payload, strlen(payload));
    need(close(fd) == 0 && renameat(dirs.stage, name, dirs.inbox, name) == 0, "publish completed input");
}

static uint64_t update_hash(uint64_t hash, const unsigned char *bytes, size_t length) {
    for (size_t i = 0; i < length; i++) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static struct result hash_file(int inbox, const char *name) {
    struct result result = {.hash = FNV_SEED};
    int fd = openat(inbox, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return (struct result){.error = errno};
    struct stat st;
    if (fstat(fd, &st) != 0) {
        result.error = errno;
    } else if (!S_ISREG(st.st_mode)) {
        result.error = EINVAL;
    } else {
        unsigned char buffer[4096];
        for (;;) {
            ssize_t n = read(fd, buffer, sizeof buffer);
            if (n == 0) break;
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) { result.error = errno; break; }
            result.bytes += (uint64_t)n;
            if (result.bytes > FILE_LIMIT) { result.error = EFBIG; break; }
            result.hash = update_hash(result.hash, buffer, (size_t)n);
        }
    }
    close(fd);
    return result;
}

static void worker_loop(int inbox, int commands, int results, bool inject) {
    struct job job;
    ssize_t n;
    while ((n = read(commands, &job, sizeof job)) != 0) {
        if (n < 0 && errno == EINTR) continue;
        need(n == sizeof job, "read whole work request");
        if (inject && job.attempt == 1 && strcmp(job.name, "crash.job") == 0) _exit(7);
        const struct result result = hash_file(inbox, job.name);
        need(write(results, &result, sizeof result) == sizeof result, "send whole result");
    }
    _exit(0);
}

static void start_worker(struct service *service, unsigned slot) {
    int commands[2], results[2];
    need(pipe(commands) == 0 && pipe(results) == 0, "make private worker pipes");
    need(fcntl(results[0], F_SETFL, O_NONBLOCK) == 0, "make result pipe nonblocking");
    pid_t pid = fork();
    need(pid >= 0, "fork worker");
    if (pid == 0) {
        close(service->ep); close(service->notify);
        close(service->dirs.stage); close(service->dirs.outbox);
        close(commands[1]); close(results[0]);
        for (unsigned i = 0; i < WORKERS; i++) {
            if (service->workers[i].pid > 0) {
                close(service->workers[i].command);
                close(service->workers[i].result);
            }
        }
        worker_loop(service->dirs.inbox, commands[0], results[1], service->inject_exit);
    }
    close(commands[0]); close(results[1]);
    service->workers[slot] = (struct worker){.pid = pid, .command = commands[1], .result = results[0]};
    struct epoll_event ready = {.events = EPOLLIN | EPOLLHUP, .data.u64 = slot + 2};
    need(epoll_ctl(service->ep, EPOLL_CTL_ADD, results[0], &ready) == 0, "watch worker results");
}

static void assign(struct worker *worker, struct job job) {
    worker->job = job;
    ssize_t n;
    do { n = write(worker->command, &worker->job, sizeof worker->job); } while (n < 0 && errno == EINTR);
    need(n == sizeof worker->job || (n < 0 && errno == EPIPE), "dispatch job");
}

static bool enqueue(struct service *service, const char *name) {
    if (!job_name(name) || exists(service->dirs.outbox, name)) return false;
    for (unsigned i = 0; i < WORKERS; i++) {
        if (strcmp(service->workers[i].job.name, name) == 0) return false;
    }
    for (unsigned i = 0; i < service->queued; i++) {
        if (strcmp(service->pending[i].name, name) == 0) return false;
    }
    if (service->queued == PENDING_LIMIT) {
        service->rescan = true;
        return false;
    }
    struct job *job = &service->pending[service->queued++];
    strcpy(job->name, name);
    job->attempt = 1;
    return true;
}

static void scan(struct service *service) {
    service->rescan = false;
    service->scans++;
    int fd = openat(service->dirs.inbox, ".", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    need(fd >= 0, "open directory for reconciliation");
    DIR *dir = fdopendir(fd);
    need(dir != NULL, "start directory scan");
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { need(errno == 0, "finish directory scan"); break; }
        enqueue(service, entry->d_name);
        if (service->rescan) break;
    }
    need(closedir(dir) == 0, "close directory scan");
}

static void accept_notifications(struct service *service) {
    _Alignas(struct inotify_event) char buffer[8192];
    for (;;) {
        ssize_t n = read(service->notify, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) return;
        need(n > 0, "read directory events");
        size_t remaining = (size_t)n;
        for (const char *cursor = buffer; remaining;) {
            need(remaining >= sizeof(struct inotify_event), "complete event header");
            const struct inotify_event *event = (const void *)cursor;
            need(event->len <= remaining - sizeof *event &&
                 event->len % _Alignof(struct inotify_event) == 0, "complete event name");
            if (event->mask & IN_Q_OVERFLOW) {
                service->overflow++;
                service->rescan = true;
                puts("OVERFLOW inotify queue full; reconciling with inbox");
            }
            need(!(event->mask & IN_IGNORED), "inbox watch remains active");
            if ((event->mask & IN_MOVED_TO) && !(event->mask & IN_ISDIR)) {
                need(event->len && memchr(event->name, 0, event->len), "terminated event name");
                if (enqueue(service, event->name)) service->event_discovered++;
            }
            size_t size = sizeof *event + event->len;
            cursor += size;
            remaining -= size;
        }
    }
}

static void commit_result(struct service *service, struct worker *worker) {
    if (!worker->job.name[0]) return;
    struct result result;
    ssize_t n;
    do { n = read(worker->result, &result, sizeof result); } while (n < 0 && errno == EINTR);
    if (n == 0 || (n < 0 && errno == EAGAIN)) return;
    need(n == sizeof result, "read complete worker response");
    char record[128];
    int len = snprintf(record, sizeof record, "fnv1a64=%016" PRIx64 " bytes=%" PRIu64 " error=%d\n",
                       result.hash, result.bytes, result.error);
    need(len > 0 && (size_t)len < sizeof record, "format result");
    int outbox = service->dirs.outbox;
    need(!exists(outbox, worker->job.name), "commit each job only once");
    int fd = openat(outbox, ".pending", O_CREAT | O_TRUNC | O_NOFOLLOW | O_WRONLY | O_CLOEXEC, 0600);
    need(fd >= 0, "open temporary result");
    write_all(fd, record, (size_t)len);
    need(close(fd) == 0 && renameat(outbox, ".pending", outbox, worker->job.name) == 0, "publish result atomically");
    if (service->verbose) printf("COMMIT %s attempt=%u %s", worker->job.name, worker->job.attempt, record);
    service->committed++;
    worker->job.name[0] = 0;
}

static void service_workers(struct service *service) {
    for (unsigned i = 0; i < WORKERS; i++) {
        struct worker *worker = &service->workers[i];
        int status;
        pid_t ended = waitpid(worker->pid, &status, WNOHANG);
        if (ended == -1 && errno == EINTR) continue;
        need(ended == 0 || ended == worker->pid, "check worker status");
        commit_result(service, worker);
        if (ended == 0) continue;

        struct job lost = worker->job;
        need(service->recovered < 8 && (!lost.name[0] || lost.attempt < 3), "limit restart attempts");
        need(epoll_ctl(service->ep, EPOLL_CTL_DEL, worker->result, NULL) == 0, "unregister exited worker");
        close(worker->command); close(worker->result);
        worker->pid = 0;
        start_worker(service, i);
        service->recovered++;
        printf("RESTART slot=%u old_pid=%ld new_pid=%ld\n", i, (long)ended, (long)worker->pid);
        if (lost.name[0]) {
            lost.attempt++;
            printf("REQUEUE %s attempt=%u\n", lost.name, lost.attempt);
            assign(worker, lost);
        }
    }
}

static void pump(struct service *service) {
    if (service->rescan && service->queued == 0) scan(service);
    bool busy = false;
    for (unsigned i = 0; i < WORKERS; i++) {
        struct worker *worker = &service->workers[i];
        if (!worker->job.name[0] && service->queued) {
            assign(worker, service->pending[0]);
            --service->queued;
            memmove(service->pending, service->pending + 1, service->queued * sizeof service->pending[0]);
        }
        busy |= worker->job.name[0] != 0;
    }
    struct epoll_event events[WORKERS + 1];
    // HUP usually reports worker exit through epoll. A short wait timeout
    // while there are jobs in flight also covers delayed pipe teardown.
    int count = epoll_wait(service->ep, events, WORKERS + 1, busy ? 50 : -1);
    if (count < 0 && errno == EINTR) return;
    need(count >= 0, "wait for files and worker responses");
    for (int i = 0; i < count; i++) {
        if (events[i].data.u64 == WATCH_EVENT) accept_notifications(service);
    }
    service_workers(service);
}

static void start(struct service *service, const char *root) {
    service->dirs = open_folders(root);
    service->notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    service->ep = epoll_create1(EPOLL_CLOEXEC);
    need(service->notify >= 0 && service->ep >= 0, "create file watcher and epoll");
    char inbox[PATH_MAX];
    int len = snprintf(inbox, sizeof inbox, "%s/inbox", root);
    need(len > 0 && (size_t)len < sizeof inbox, "inbox path fits");
    need(inotify_add_watch(service->notify, inbox, IN_MOVED_TO | IN_ONLYDIR) >= 0, "watch atomic moves into inbox");
    struct epoll_event ready = {.events = EPOLLIN, .data.u64 = WATCH_EVENT};
    need(epoll_ctl(service->ep, EPOLL_CTL_ADD, service->notify, &ready) == 0, "monitor inbox in epoll");
    for (unsigned i = 0; i < WORKERS; i++) start_worker(service, i);
    scan(service);
    printf("WATCH_READY %s\n", inbox);
}

static void stop(struct service *service) {
    for (unsigned i = 0; i < WORKERS; i++) close(service->workers[i].command);
    for (unsigned i = 0; i < WORKERS; i++) {
        int status;
        need(waitpid(service->workers[i].pid, &status, 0) == service->workers[i].pid &&
             WIFEXITED(status) && WEXITSTATUS(status) == 0, "reap service worker");
        close(service->workers[i].result);
    }
    close(service->notify); close(service->ep);
}

static void verify_output(struct folders dirs, const char *name, const char *payload) {
    char buffer[128], expected[128];
    int fd = openat(dirs.outbox, name, O_RDONLY | O_NOFOLLOW);
    need(fd >= 0, "find output for each job");
    ssize_t n = read(fd, buffer, sizeof buffer - 1);
    need(n > 0, "read output");
    buffer[n] = 0;
    close(fd);
    size_t len = strlen(payload);
    snprintf(expected, sizeof expected, "fnv1a64=%016" PRIx64 " bytes=%zu error=0\n",
             update_hash(FNV_SEED, (const unsigned char *)payload, len), len);
    if (strcmp(buffer, expected) != 0) {
        fprintf(stderr, "FAIL %s expected %sactual %s", name, expected, buffer);
        exit(1);
    }
    need(unlinkat(dirs.outbox, name, 0) == 0 && unlinkat(dirs.inbox, name, 0) == 0, "clean verified file pair");
}

static void force_overflow(struct folders dirs) {
    // Linux's configured queue is usually larger than the StarryOS queue.
    // Alternate ignored destination names: Linux can merge consecutive events
    // with the same watch, mask and name, even when move cookies differ.
    FILE *limit_file = fopen("/proc/sys/fs/inotify/max_queued_events", "r");
    if (!limit_file) return;
    unsigned limit;
    need(fscanf(limit_file, "%u", &limit) == 1 && limit <= 65536, "read Linux inotify queue limit");
    fclose(limit_file);
    if (limit < BURST) return;
    int fd = openat(dirs.stage, ".pulse", O_CREAT | O_EXCL | O_WRONLY, 0600);
    need(fd >= 0 && close(fd) == 0, "create overflow stimulus");
    for (unsigned i = BURST; i <= limit; i++) {
        const char *destination = i % 2 ? ".pulse-a" : ".pulse-b";
        need(renameat(dirs.stage, ".pulse", dirs.inbox, destination) == 0 &&
             renameat(dirs.inbox, destination, dirs.stage, ".pulse") == 0, "fill event queue with distinct moves");
    }
    need(unlinkat(dirs.stage, ".pulse", 0) == 0, "clean overflow stimulus");
}

static void verify(void) {
    alarm(60);
    // StarryOS's current tmpfs loses cached file contents across rename.
    // Use the guest's disk-backed home directory by default.
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = getenv("HOME");
    if (!base || !*base) base = "/tmp";
    char root[PATH_MAX];
    int len = snprintf(root, sizeof root, "%s/folder-watch-XXXXXX", base);
    need(len > 0 && (size_t)len < sizeof root, "private demo path fits");
    need(mkdtemp(root) != NULL, "create private demo workspace");
    struct service service = {.inject_exit = true, .verbose = true};
    start(&service, root);
    struct folders dirs = service.dirs;
    const char *payload = "StarryOS event-driven job\n";

    puts("[1/3] Publish a file by rename");
    put(dirs, "sample.job", payload);
    while (service.committed < 1) pump(&service);
    need(service.event_discovered == 1, "discover first job via inotify");

    puts("[2/3] Worker exits after receiving a job");
    put(dirs, "crash.job", "Recover this job\n");
    put(dirs, "other.job", "Finish while the worker is replaced\n");
    while (service.committed < 3) pump(&service);
    need(service.recovered == 1, "replace exactly one worker");

    puts("[3/3] Pause the event reader and fill the kernel queue");
    service.verbose = false;
    char name[NAME_LIMIT];
    // Fill with ignored moves first on Linux, so real jobs also exercise rescan.
    force_overflow(dirs);
    for (unsigned i = 0; i < BURST; i++) {
        snprintf(name, sizeof name, "batch-%03u.job", i);
        put(dirs, name, name);
    }
    while (service.committed < 3 + BURST) pump(&service);
    need(service.overflow > 0 && service.scans > 1 && service.committed == 3 + BURST,
         "reconcile all jobs after a real queue overflow");
    stop(&service);
    verify_output(dirs, "sample.job", payload);
    verify_output(dirs, "crash.job", "Recover this job\n");
    verify_output(dirs, "other.job", "Finish while the worker is replaced\n");
    for (unsigned i = 0; i < BURST; i++) {
        snprintf(name, sizeof name, "batch-%03u.job", i);
        verify_output(dirs, name, name);
    }
    close_folders(dirs);
    need(chdir(root) == 0 && rmdir("inbox") == 0 && rmdir("outbox") == 0 && rmdir("stage") == 0 &&
         chdir("/") == 0 && rmdir(root) == 0, "clean private demo workspace");
    printf("RECONCILED submitted=%u verified=%u overflow=%u workers_replaced=%u PASS\n",
           3u + BURST, service.committed, service.overflow, service.recovered);
    puts("VERIFY: PASS");
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    need(signal(SIGPIPE, SIG_IGN) != SIG_ERR, "handle a worker that has exited");
    if (argc == 2 && strcmp(argv[1], "verify") == 0) {
        verify();
    } else if (argc == 3 && strcmp(argv[1], "watch") == 0) {
        need(mkdir(argv[2], 0700) == 0 || errno == EEXIST, "create service workspace");
        struct service service = {.verbose = true};
        start(&service, argv[2]);
        for (;;) pump(&service);
    } else if (argc == 5 && strcmp(argv[1], "put") == 0) {
        struct folders dirs = open_folders(argv[2]);
        put(dirs, argv[3], argv[4]);
        close_folders(dirs);
    } else {
        fprintf(stderr, "Usage: %s verify | watch ROOT | put ROOT NAME.job PAYLOAD\n", argv[0]);
        return 2;
    }
    return 0;
}
