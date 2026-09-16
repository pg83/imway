// A child of the compositor — started through `-- CMD` at boot and again
// from the launcher once the session has worked — records what it
// inherited. It must have stdio on /dev/null, no descriptor beyond them, an
// empty signal mask with default dispositions, the compositor's environment
// plus WAYLAND_DISPLAY, the compositor as its parent and the parent's
// process group. Then it waits for the SIGTERM a child gets when the
// compositor exits and records that too.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int killed_fd = -1;

static void killed(int sig) {
    (void)sig;
    (void)!write(killed_fd, "killed\n", 7);
    _exit(0);
}

static int report(const char* runtime, const char* tag, const char* value) {
    char path[512];

    snprintf(path, sizeof(path), "%s/spawn-result-%s", runtime, tag);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    if (fd < 0) return 1;
    (void)!write(fd, value, strlen(value));
    close(fd);

    return strcmp(value, "ok\n") != 0;
}

static const char* check_stdio(void) {
    struct stat null_stat;

    if (stat("/dev/null", &null_stat) != 0) return "cannot stat /dev/null\n";

    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
        struct stat fd_stat;

        if (fstat(fd, &fd_stat) != 0 || !S_ISCHR(fd_stat.st_mode) ||
            fd_stat.st_rdev != null_stat.st_rdev) {
            return "stdio is not /dev/null\n";
        }
    }

    return NULL;
}

// every open descriptor, not a fixed range: whatever the compositor or one
// of its libraries opened without close-on-exec shows up here
static const char* check_fds(char* detail, size_t cap) {
    DIR* dir = opendir("/proc/self/fd");

    if (!dir) return "cannot list /proc/self/fd\n";

    int self = dirfd(dir);
    struct dirent* entry;

    while ((entry = readdir(dir))) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;

        int fd = atoi(entry->d_name);

        if (fd <= STDERR_FILENO || fd == self) continue;

        char link[256];
        char target[256];
        ssize_t n;

        snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        n = readlink(link, target, sizeof(target) - 1);
        target[n > 0 ? n : 0] = 0;
        snprintf(detail, cap, "leaked fd %d -> %s\n", fd, target);
        closedir(dir);

        return detail;
    }

    closedir(dir);

    return NULL;
}

static const char* check_signals(void) {
    sigset_t mask;

    if (sigprocmask(SIG_BLOCK, NULL, &mask) != 0 || !sigisemptyset(&mask)) {
        return "signal mask is not empty\n";
    }

    // the fault signals are left out: a sanitizer runtime in this very
    // probe installs its own handlers for them
    int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGCHLD, SIGUSR1};

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        struct sigaction action;

        if (sigaction(signals[i], NULL, &action) != 0 || action.sa_handler != SIG_DFL) {
            return "a signal disposition is not the default\n";
        }
    }

    return NULL;
}

static const char* check_parent(void) {
    char path[64];
    char comm[64] = "";

    snprintf(path, sizeof(path), "/proc/%d/comm", (int)getppid());

    FILE* f = fopen(path, "re");

    if (!f) return "cannot read the parent's name\n";
    (void)!fgets(comm, sizeof(comm), f);
    fclose(f);

    if (strncmp(comm, "imway", 5) != 0) return "the parent is not the compositor\n";
    if (getpgrp() != getpgid(getppid())) return "not in the parent's process group\n";

    return NULL;
}

int main(int argc, char** argv) {
    const char* tag = argc > 1 ? argv[1] : "boot";
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    const char* display = getenv("WAYLAND_DISPLAY");

    if (!runtime) return 1;

    if (!display || strcmp(display, "imway-test") != 0 || !getenv("PATH")) {
        return report(runtime, tag, "bad environment\n");
    }

    char detail[512];
    const char* problem = check_stdio();

    if (!problem) problem = check_fds(detail, sizeof(detail));
    if (!problem) problem = check_signals();
    if (!problem) problem = check_parent();
    if (problem) return report(runtime, tag, problem);

    char killed_path[512];

    snprintf(killed_path, sizeof(killed_path), "%s/spawn-killed-%s", runtime, tag);
    killed_fd = open(killed_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    if (killed_fd < 0) return report(runtime, tag, "kill marker open failed\n");

    signal(SIGTERM, killed);
    alarm(120);

    if (report(runtime, tag, "ok\n") != 0) return 1;

    for (;;) pause();
}
