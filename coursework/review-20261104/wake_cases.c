#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(long ms) {
    struct timespec t = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}

int main(void) {
    int fd = eventfd(0, 0), ready[2];
    if (fd < 0 || pipe(ready)) return 2;
    uint64_t full = UINT64_MAX - 1, one = 1, value;
    if (write(fd, &full, 8) != 8) return 3;
    pid_t child = fork();
    if (child < 0) return 4;
    if (!child) {
        char r = 'r';
        if (write(ready[1], &r, 1) != 1) _exit(5);
        _exit(write(fd, &one, 8) == 8 ? 0 : 6);
    }
    char r;
    if (read(ready[0], &r, 1) != 1) return 7;
    sleep_ms(250);
    errno = 0;
    long bad = syscall(SYS_read, fd, (void *)1, 8);
    int bad_errno = errno;
    sleep_ms(300);
    int status;
    pid_t before = waitpid(child, &status, WNOHANG);
    printf("BAD_READ rc=%ld errno=%d; WRITER_FINISHED_BEFORE_RESCUE=%d\n", bad, bad_errno, before == child);
    if (before == 0) {
        if (write(fd, &one, 8) != 8 || read(fd, &value, 8) != 8) return 8;
        sleep_ms(300);
        pid_t after = waitpid(child, &status, WNOHANG);
        printf("WRITER_FINISHED_AFTER_VALID_READ=%d\n", after == child);
        if (after == 0) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    }
    return 0;
}
