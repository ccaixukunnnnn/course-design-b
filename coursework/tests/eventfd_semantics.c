#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <unistd.h>

static void require(int condition, const char *message) {
    if (!condition) { perror(message); exit(1); }
}

int main(void) {
    uint64_t value = 0;
    int fd = eventfd(3, EFD_SEMAPHORE | EFD_NONBLOCK);
    require(fd >= 0, "eventfd semaphore");
    for (int i = 0; i < 3; i++) {
        require(read(fd, &value, sizeof value) == sizeof value, "semaphore read");
        if (value != 1) { fprintf(stderr, "expected 1, got %llu\n", (unsigned long long)value); return 1; }
    }
    errno = 0;
    require(read(fd, &value, sizeof value) == -1 && errno == EAGAIN, "empty semaphore");
    value = 2;
    require(write(fd, &value, sizeof value) == sizeof value, "semaphore write");
    require(read(fd, &value, sizeof value) == sizeof value && value == 1, "semaphore reread");
    close(fd);
    fd = eventfd(3, EFD_NONBLOCK);
    require(fd >= 0, "eventfd normal");
    require(read(fd, &value, sizeof value) == sizeof value && value == 3, "normal read");
    errno = 0;
    require(read(fd, &value, sizeof value) == -1 && errno == EAGAIN, "empty normal");
    close(fd);
    puts("eventfd semantics: PASS");
    return 0;
}
