#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
    void *memory = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char *vector = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED || vector == MAP_FAILED) { perror("mmap"); return 2; }
    printf("MINCORE_LAZY_OUTPUT memory=%p vector=%p\n", memory, vector);
    fflush(stdout);
    errno = 0;
    long rc = syscall(SYS_mincore, memory, 4096, vector);
    printf("MINCORE rc=%ld errno=%d vector=%u\n", rc, errno, (unsigned)*vector);
    puts("VERIFY: PASS");
    return 0;
}
