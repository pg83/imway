#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int killed_fd = -1;

static void killed(int sig) {
    (void)sig;
    (void)!write(killed_fd, "killed\n", 7);
    _exit(0);
}

static int write_result(const char* runtime, const char* value) {
    char path[512];

    snprintf(path, sizeof(path), "%s/supervisor-result", runtime);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    if (fd < 0) return 1;
    (void)!write(fd, value, strlen(value));
    close(fd);

    return 0;
}

int main(void) {
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    const char* display = getenv("WAYLAND_DISPLAY");

    if (!runtime || !display || strcmp(display, "imway-test") != 0) {
        return write_result(runtime ? runtime : "/tmp", "bad environment\n");
    }

    for (int fd = 3; fd < 256; fd++) {
        errno = 0;

        if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) {
            return write_result(runtime, "leaked fd\n");
        }
    }

    if (getpgrp() != getppid()) {
        return write_result(runtime, "wrong process group\n");
    }

    char killed_path[512];

    snprintf(killed_path, sizeof(killed_path), "%s/supervisor-killed", runtime);
    killed_fd = open(killed_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    if (killed_fd < 0) {
        return write_result(runtime, "kill marker open failed\n");
    }

    int ready[2];

    if (pipe2(ready, O_CLOEXEC) != 0) {
        return write_result(runtime, "probe pipe failed\n");
    }

    pid_t child = fork();

    if (child < 0) {
        return write_result(runtime, "probe fork failed\n");
    }

    if (child == 0) {
        close(ready[0]);
        signal(SIGTERM, killed);
        (void)!write(ready[1], "x", 1);
        close(ready[1]);

        for (;;) pause();
    }

    close(ready[1]);

    char byte;

    if (read(ready[0], &byte, 1) != 1) {
        return write_result(runtime, "probe child did not arm\n");
    }

    close(ready[0]);
    close(killed_fd);
    killed_fd = -1;

    if (write_result(runtime, "ok\n") != 0) {
        return 1;
    }

    char release[512];

    snprintf(release, sizeof(release), "%s/supervisor-release", runtime);

    while (access(release, F_OK) != 0) {
        usleep(10000);
    }

    return 0;
}
