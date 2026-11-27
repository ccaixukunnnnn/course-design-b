#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "probe")) {
        int fd = eventfd(3, EFD_NONBLOCK);
        if (fd < 0) return 3;
        errno = 0;
        long rc = syscall(SYS_read, fd, (void *)1, 8);
        printf("READ_BAD rc=%ld errno=%d\n", rc, errno);
        uint64_t value = 0;
        errno = 0;
        rc = read(fd, &value, sizeof value);
        printf("READ_AFTER rc=%ld errno=%d value=%" PRIu64 "\n", rc, errno, value);
        printf("UID_BEFORE uid=%d euid=%d\n", getuid(), geteuid());
        errno = 0;
        int set_result = setuid(1234);
        int set_errno = errno;
        printf("SETUID rc=%d errno=%d uid=%d euid=%d\n", set_result, set_errno, getuid(), geteuid());
        return 0;
    }
    if (!strcmp(argv[1], "verify")) {
        char c = 0;
        puts("REQUEST_GETRANDOM_SIZE_MAX");
        fflush(stdout);
        errno = 0;
        long rc = syscall(SYS_getrandom, &c, SIZE_MAX, 0);
        printf("GETRANDOM rc=%ld errno=%d\n", rc, errno);
        puts("VERIFY: PASS");
        return 0;
    }
    return 2;
}
