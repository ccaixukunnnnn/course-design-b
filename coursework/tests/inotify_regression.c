#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

static void need(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s (errno=%d)\n", what, errno); exit(1); }
}

struct batch {
    _Alignas(struct inotify_event) char data[4096];
    size_t length, offset;
};

static void fill(struct batch *batch, int fd) {
    ssize_t n = read(fd, batch->data, sizeof batch->data);
    need(n > 0, "read event batch");
    batch->length = (size_t)n;
    batch->offset = 0;
}

static uint32_t expect(struct batch *batch, int wd, uint32_t mask, const char *name) {
    size_t remaining = batch->length - batch->offset;
    need(remaining >= sizeof(struct inotify_event), "event header fits");
    const struct inotify_event *event = (const void *)(batch->data + batch->offset);
    need(event->len <= remaining - sizeof *event && event->len % _Alignof(struct inotify_event) == 0,
         "event is fully present and aligned");
    need(event->wd == wd && event->mask == mask, "watch id and event mask");
    need(event->len ? memchr(event->name, 0, event->len) && strcmp(event->name, name) == 0 : *name == 0,
         "event name");
    batch->offset += sizeof *event + event->len;
    return event->cookie;
}

int main(void) {
    alarm(12);
    char root[] = "/tmp/inotify-test-XXXXXX";
    need(mkdtemp(root) != NULL, "make temporary directory");
    need(chdir(root) == 0 && mkdir("from", 0700) == 0 && mkdir("to", 0700) == 0, "prepare directories");
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    need(fd >= 0 && ep >= 0, "create inotify and epoll");
    int flags = fcntl(fd, F_GETFL), descriptor_flags = fcntl(fd, F_GETFD);
    need(flags >= 0 && (flags & O_NONBLOCK) && descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC),
         "inotify flags");
    uint32_t changes = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM;
    int from = inotify_add_watch(fd, "from", changes | IN_ONLYDIR);
    int to = inotify_add_watch(fd, "to", changes | IN_DELETE_SELF);
    need(from > 0 && to > 0 && from != to, "subscribe to both directories");
    need(inotify_add_watch(fd, "./from/.", IN_MOVE_SELF | IN_MASK_ADD) == from, "path alias keeps watch id");
    struct epoll_event ev = {.events = EPOLLIN, .data.u64 = 71};
    need(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) == 0, "register watcher with epoll");
    struct batch batch;
    need(read(fd, batch.data, sizeof batch.data) < 0 && errno == EAGAIN, "new queue is empty");

    int file = open("from/input.job", O_CREAT | O_WRONLY | O_EXCL, 0600);
    need(file >= 0 && close(file) == 0, "create regular file");
    need(epoll_wait(ep, &ev, 1, 1000) == 1 && ev.data.u64 == 71 && (ev.events & EPOLLIN),
         "creation wakes epoll");
    need(read(fd, batch.data, sizeof(struct inotify_event)) < 0 && errno == EINVAL, "reject partial event buffer");
    fill(&batch, fd);
    need(expect(&batch, from, IN_CREATE, "input.job") == 0 && batch.offset == batch.length, "one create event");
    file = open("from/input.job", O_CREAT | O_WRONLY, 0600);
    need(file >= 0 && close(file) == 0, "reopen existing file");
    need(rename("from/missing.job", "to/skipped.job") == -1 && errno == ENOENT, "failed rename");
    need(epoll_wait(ep, &ev, 1, 0) == 0, "reopen and failed rename produce no directory event");

    need(rename("from/input.job", "to/ready.job") == 0, "move file between watched directories");
    fill(&batch, fd);
    uint32_t cookie = expect(&batch, from, IN_MOVED_FROM, "input.job");
    need(cookie != 0 && cookie == expect(&batch, to, IN_MOVED_TO, "ready.job") && batch.offset == batch.length,
         "paired rename cookie");
    need(unlink("to/ready.job") == 0, "remove file");
    fill(&batch, fd);
    expect(&batch, to, IN_DELETE, "ready.job");
    need(batch.offset == batch.length, "one delete event");
    puts("PASS create, complete records, rename cookie and delete");

    need(rename("from", "moved") == 0 && mkdir("moved/child", 0700) == 0, "move watched directory and create child");
    fill(&batch, fd);
    expect(&batch, from, IN_MOVE_SELF, "");
    expect(&batch, from, IN_CREATE | IN_ISDIR, "child");
    need(inotify_rm_watch(fd, from) == 0, "remove watch");
    fill(&batch, fd);
    expect(&batch, from, IN_IGNORED, "");
    need(inotify_rm_watch(fd, from) < 0 && errno == EINVAL, "removed watch is invalid");
    need(rmdir("moved/child") == 0, "remove child after moved watch is removed");
    need(rmdir("moved") == 0, "remove moved directory");
    need(rmdir("to") == 0, "remove watched directory");
    fill(&batch, fd);
    expect(&batch, to, IN_DELETE_SELF, "");
    expect(&batch, to, IN_IGNORED, "");
    need(batch.offset == batch.length, "no event for removed watch");
    close(ep); close(fd);
    need(chdir("/") == 0 && rmdir(root) == 0, "clean test directory");
    puts("PASS watches follow directory objects and emit IN_IGNORED");
    puts("VERIFY: PASS");
    return 0;
}
