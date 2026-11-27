#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

static int print_bytes(const char *path) {
    unsigned char bytes[32];
    int fd = open(path, O_RDONLY);
    if (fd < 0 || read(fd, bytes, sizeof bytes) != sizeof bytes) return 1;
    printf("RANDOM %s=", path);
    for (unsigned i = 0; i < sizeof bytes; i++) printf("%02x", bytes[i]);
    putchar('\n');
    close(fd);
    return 0;
}

int main(void) {
    return print_bytes("/dev/random") || print_bytes("/dev/urandom");
}
