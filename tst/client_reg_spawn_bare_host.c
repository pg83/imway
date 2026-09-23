// Runs a command as it would start on a bare, old host: with a descriptor
// its starter forgot to close (fd 7, on /dev/null, without close-on-exec)
// and on a kernel without close_range(2), which a seccomp filter answers
// with ENOSYS as a pre-5.9 kernel does (and as container runtimes that do
// not know the call yet do). With no command it only says whether this
// build can install the filter: exit 0 if so, 127 if not.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char** argv) {
#ifndef __NR_close_range
    (void)argc;
    (void)argv;
    fprintf(stderr, "SKIP: these headers do not know close_range\n");

    return 127;
#else
    if (argc < 2) {
        return 0;
    }

    int stray = open("/dev/null", O_RDONLY);

    if (stray < 0 || dup2(stray, 7) != 7) {
        perror("stray descriptor");

        return 1;
    }

    if (stray != 7) {
        close(stray);
    }

    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_close_range, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = {
        .len = sizeof(filter) / sizeof(filter[0]),
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 || prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) {
        perror("seccomp");

        return 1;
    }

    execv(argv[1], argv + 1);
    perror(argv[1]);

    return 1;
#endif
}
