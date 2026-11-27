#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/seccomp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s (errno=%d)\n", what, errno);
        exit(1);
    }
}

static void probe(void) {
    unsigned cpu = 0xdeadbeef, node = 0xdeadbeef;
    errno = 0;
    long ret = syscall(SYS_getcpu, &cpu, &node, NULL);
    printf("PROBE getcpu ret=%ld errno=%d cpu=%u node=%u\n", ret, errno, cpu, node);
    errno = 0;
    ret = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, NULL);
    printf("PROBE seccomp-filter ret=%ld errno=%d\n", ret, errno);
    errno = 0;
    ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, NULL, 0, 0);
    printf("PROBE prctl-seccomp ret=%ld errno=%d\n", ret, errno);
}

static void verify(void) {
    unsigned cpu = UINT32_MAX, node = UINT32_MAX;
    errno = 0;
    check(syscall(SYS_getcpu, &cpu, &node, NULL) == 0, "getcpu both outputs");
    check(cpu == 0 && node == 0, "getcpu on single CPU/node QEMU");
    check(syscall(SYS_getcpu, NULL, NULL, NULL) == 0, "getcpu null outputs");
    cpu = UINT32_MAX;
    check(syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 && cpu == 0, "getcpu cpu only");
    node = UINT32_MAX;
    check(syscall(SYS_getcpu, NULL, &node, NULL) == 0 && node == 0, "getcpu node only");
    errno = 0;
    check(syscall(SYS_getcpu, (unsigned *)1, NULL, NULL) == -1 && errno == EFAULT,
          "getcpu invalid cpu pointer");
    errno = 0;
    check(syscall(SYS_getcpu, NULL, (unsigned *)1, NULL) == -1 && errno == EFAULT,
          "getcpu invalid node pointer");
    errno = 0;
    check(syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, NULL) == -1 && errno == ENOSYS,
          "seccomp filter must fail closed");
    errno = 0;
    check(syscall(SYS_seccomp, SECCOMP_GET_ACTION_AVAIL, 0, NULL) == -1 && errno == ENOSYS,
          "seccomp query must not report success");
    errno = 0;
    check(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, NULL, 0, 0) == -1 && errno == ENOSYS,
          "prctl seccomp filter must fail closed");
    errno = 0;
    check(prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, 0, 0, 0) == -1 && errno == ENOSYS,
          "prctl seccomp strict must fail closed");

    int fd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    check(fd >= 0, "eventfd nonblocking semaphore");
    uint64_t value = 7;
    errno = 0;
    check(read(fd, &value, 8) == -1 && errno == EAGAIN, "empty eventfd");
    value = 2;
    check(write(fd, &value, 8) == 8, "eventfd write");
    check(read(fd, &value, 8) == 8 && value == 1, "first semaphore token");
    check(read(fd, &value, 8) == 8 && value == 1, "second semaphore token");
    errno = 0;
    check(read(fd, &value, 8) == -1 && errno == EAGAIN, "semaphore drained");
    value = UINT64_MAX;
    errno = 0;
    check(write(fd, &value, 8) == -1 && errno == EINVAL, "invalid eventfd write");
    errno = 0;
    check(read(fd, &value, 7) == -1 && errno == EINVAL, "short eventfd read");
    value = UINT64_MAX - 1;
    check(write(fd, &value, 8) == 8, "eventfd fill to max");
    value = 1;
    errno = 0;
    check(write(fd, &value, 8) == -1 && errno == EAGAIN, "full eventfd write");
    close(fd);
    fd = eventfd(5, 0);
    check(fd >= 0, "blocking eventfd");
    check(read(fd, &value, 8) == 8 && value == 5, "blocking-mode ready read");
    value = 1;
    check(write(fd, &value, 8) == 8, "blocking-mode ready write");
    close(fd);
    fd = eventfd(0, 0);
    check(fd >= 0, "blocking eventfd for wakeup");
    pid_t child = fork();
    check(child >= 0, "fork wakeup writer");
    if (child == 0) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000000};
        if (nanosleep(&delay, NULL) != 0) _exit(2);
        uint64_t token = 9;
        _exit(write(fd, &token, sizeof token) == sizeof token ? 0 : 3);
    }
    check(read(fd, &value, 8) == 8 && value == 9, "blocking read awakened by child");
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "wakeup writer exit");
    close(fd);
    puts("VERIFY: PASS");
}

static uint64_t ns(void) {
    struct timespec ts;
    check(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock_gettime");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void bench(void) {
    enum { WARMUP = 3000, ROUNDS = 7, PAIRS = 20000 };
    int fd = eventfd(0, EFD_NONBLOCK);
    check(fd >= 0, "benchmark eventfd");
    uint64_t one = 1, value = 0;
    for (int r = -1; r < ROUNDS; r++) {
        int pairs = r < 0 ? WARMUP : PAIRS;
        uint64_t start = ns();
        for (int i = 0; i < pairs; i++) {
            check(write(fd, &one, 8) == 8, "benchmark write");
            check(read(fd, &value, 8) == 8 && value == 1, "benchmark read");
        }
        uint64_t elapsed = ns() - start;
        if (r >= 0) printf("BENCH round=%d pairs=%d elapsed_ns=%" PRIu64 "\n", r + 1, pairs, elapsed);
    }
    close(fd);
    puts("BENCH: PASS");
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "probe")) probe();
    else if (!strcmp(argv[1], "verify")) verify();
    else if (!strcmp(argv[1], "bench")) bench();
    else return 2;
    return 0;
}
