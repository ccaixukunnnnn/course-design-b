#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d\n", message, errno); exit(1); }
}

static void sleep_ms(long ms) {
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000};
    check(nanosleep(&ts, NULL) == 0, "nanosleep");
}

static void test_random(void) {
    unsigned char buf[32] = {0};
    const char *paths[] = {"/dev/random", "/dev/urandom"};
    for (unsigned i = 0; i < 2; i++) {
        int fd = open(paths[i], O_RDWR);
        check(fd >= 0, "open random device");
        check(read(fd, buf, 0) == 0, "zero-length random read");
        check(write(fd, buf, 0) == 0, "zero-length random write");
        errno = 0;
        check(read(fd, buf, sizeof buf) == -1 && errno == ENOSYS, "random read unsupported");
        errno = 0;
        check(write(fd, buf, sizeof buf) == -1 && errno == ENOSYS, "random write unsupported");
        close(fd);
    }
    check(syscall(SYS_getrandom, NULL, 0, 0) == 0, "zero-length getrandom");
    errno = 0;
    check(syscall(SYS_getrandom, buf, 1, 0x80000000u) == -1 && errno == EINVAL,
          "getrandom rejects unknown flags");
    errno = 0;
    check(syscall(SYS_getrandom, buf, SIZE_MAX, 0) == -1 && errno == ENOSYS,
          "large getrandom fails without panic");
    puts("RANDOM: PASS (entropy unavailable, explicitly unsupported)");
}

static void test_credentials(void) {
    check(getuid() == 0 && geteuid() == 0 && getgid() == 0 && getegid() == 0,
          "documented fixed root identity");
    check(syscall(SYS_setuid, 0) == 0 && syscall(SYS_setgid, 0) == 0,
          "unchanged root identity");
    errno = 0;
    check(syscall(SYS_setuid, 1234) == -1 && errno == ENOSYS, "setuid change rejected");
    errno = 0;
    check(syscall(SYS_setuid, UINT32_MAX) == -1 && errno == EINVAL, "invalid uid");
    errno = 0;
    check(syscall(SYS_setgid, 1234) == -1 && errno == ENOSYS, "setgid change rejected");
    check(syscall(SYS_setreuid, -1, 0) == 0, "setreuid no change");
    check(syscall(SYS_setresuid, -1, 0, -1) == 0, "setresuid no change");
    check(syscall(SYS_setresgid, -1, 0, -1) == 0, "setresgid no change");
    errno = 0;
    check(syscall(SYS_setreuid, -1, 1234) == -1 && errno == ENOSYS, "setreuid rejected");
    errno = 0;
    check(syscall(SYS_setresuid, -1, 1234, -1) == -1 && errno == ENOSYS, "setresuid rejected");
    errno = 0;
    check(syscall(SYS_setresgid, -1, 1234, -1) == -1 && errno == ENOSYS, "setresgid rejected");
    errno = 0;
    check(syscall(SYS_setgroups, 0, NULL) == -1 && errno == ENOSYS, "setgroups rejected");
    struct __user_cap_header_struct cap_header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct cap_data[2] = {{0}, {0}};
    errno = 0;
    check(syscall(SYS_capset, &cap_header, cap_data) == -1 && errno == ENOSYS,
          "valid capset rejected rather than ignored");
    errno = 0;
    check(syscall(SYS_capget, &cap_header, cap_data) == -1 && errno == ENOSYS,
          "valid capget does not report fabricated capabilities");
    errno = 0;
    check(syscall(SYS_capset, NULL, NULL) == -1 && errno == ENOSYS, "capset unavailable");
    errno = 0;
    check(syscall(SYS_capget, NULL, NULL) == -1 && errno == ENOSYS, "capget unavailable");
    check(getuid() == 0 && geteuid() == 0 && getgid() == 0 && getegid() == 0,
          "identity unchanged after failed requests");
    puts("CREDENTIALS: PASS (changes explicitly unsupported)");
}

static void test_mincore(void) {
    enum { PAGE = 4096, PAGES = 257 };
    unsigned char *region = mmap(NULL, PAGE * PAGES, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char *vector = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(region != MAP_FAILED && vector != MAP_FAILED, "mincore mmap");
    check(syscall(SYS_mincore, region, PAGE * PAGES, vector) == 0, "mincore lazy output");
    for (int i = 0; i < PAGES; i++) check(vector[i] <= 1, "mincore reserved bits clear");
    region[PAGE * (PAGES - 1)] = 42;
    check(syscall(SYS_mincore, region, PAGE * PAGES, vector) == 0 && (vector[PAGES - 1] & 1),
          "mincore resident page after chunk boundary");
    check(syscall(SYS_mincore, NULL, 0, NULL) == 0, "zero-length mincore");
    errno = 0;
    check(syscall(SYS_mincore, (void *)1, 0, NULL) == -1 && errno == EINVAL,
          "mincore unaligned zero-length");
    errno = 0;
    check(syscall(SYS_mincore, region, SIZE_MAX, vector) == -1 && errno == ENOMEM,
          "mincore huge range rejected");
    errno = 0;
    check(syscall(SYS_mincore, region, PAGE, NULL) == -1 && errno == EFAULT,
          "mincore invalid output");
    void *readonly = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(readonly != MAP_FAILED, "mincore readonly output mapping");
    errno = 0;
    check(syscall(SYS_mincore, region, PAGE, readonly) == -1 && errno == EFAULT,
          "mincore readonly output returns EFAULT");
    check(munmap(readonly, PAGE) == 0, "mincore readonly cleanup");
    void *gone = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(gone != MAP_FAILED && munmap(gone, PAGE) == 0, "prepare unmapped page");
    errno = 0;
    check(syscall(SYS_mincore, gone, PAGE, vector) == -1 && errno == ENOMEM,
          "mincore unmapped range");
    check(munmap(region, PAGE * PAGES) == 0 && munmap(vector, PAGE) == 0, "mincore cleanup");
    puts("MINCORE: PASS");
}

static void test_eventfd_wakeup(void) {
    int fd = eventfd(0, 0), ready[2];
    check(fd >= 0 && pipe(ready) == 0, "eventfd/pipe");
    uint64_t full = UINT64_MAX - 1, one = 1;
    check(write(fd, &full, 8) == 8, "fill eventfd");
    pid_t child = fork();
    check(child >= 0, "fork writer");
    if (child == 0) {
        char b = 'x';
        if (write(ready[1], &b, 1) != 1) _exit(2);
        _exit(write(fd, &one, 8) == 8 ? 0 : 3);
    }
    char b;
    check(read(ready[0], &b, 1) == 1, "writer ready");
    sleep_ms(250);
    errno = 0;
    check(syscall(SYS_read, fd, (void *)1, 8) == -1 && errno == EFAULT,
          "failed eventfd user copy");
    int status = 0;
    pid_t result = 0;
    for (int i = 0; i < 20 && result == 0; i++) {
        sleep_ms(50);
        result = waitpid(child, &status, WNOHANG);
    }
    if (result == 0) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    check(result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "blocked writer wakes after failed read copy");
    uint64_t remaining = 0;
    check(read(fd, &remaining, sizeof remaining) == sizeof remaining && remaining == 1,
          "failed read consumed full count; writer supplied one");
    close(fd); close(ready[0]); close(ready[1]);
    puts("EVENTFD WAKEUP: PASS");
}

int main(void) {
    test_random();
    test_credentials();
    test_mincore();
    test_eventfd_wakeup();
    puts("VERIFY: PASS");
    return 0;
}
