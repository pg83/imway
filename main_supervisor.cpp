#include "main_supervisor.h"

#include "composer.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr size_t maxPacket = 64 * 1024;

    struct SpawnRequest {
        u32 size;
        u32 argCount;
        u32 envCount;
    };

    struct SupervisorImpl: Supervisor {
        void spawn(const SupervisorSpawn& spec) override;
    };

    volatile sig_atomic_t processGroup = -1;

    void terminate(int signal) {
        kill(-(pid_t)processGroup, signal);
        _exit(128 + signal);
    }

    bool installSignals() {
        struct sigaction action{};

        sigemptyset(&action.sa_mask);
        action.sa_handler = terminate;

        int terminateSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};

        for (int signal : terminateSignals) {
            if (sigaction(signal, &action, nullptr) != 0) {
                return false;
            }
        }

        action.sa_handler = SIG_IGN;
        action.sa_flags = SA_NOCLDWAIT | SA_NOCLDSTOP;

        return sigaction(SIGCHLD, &action, nullptr) == 0 && sigaction(SIGPIPE, &action, nullptr) == 0;
    }

    [[noreturn]] void finish(int code) {
        sigset_t blocked;

        sigemptyset(&blocked);
        sigaddset(&blocked, SIGTERM);
        sigprocmask(SIG_BLOCK, &blocked, nullptr);
        kill(-(pid_t)processGroup, SIGTERM);
        _exit(code);
    }

    void resetSignals() {
        sigset_t empty;

        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);

        int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGCHLD};

        for (int signal : signals) {
            ::signal(signal, SIG_DFL);
        }
    }

    bool startComposer(int argc, char** argv, int pipe) {
        Vector<char*> args((size_t)argc + 2);

        args.pushBack((char*)"/proc/self/exe");
        args.pushBack((char*)"composer");

        for (int i = 1; i < argc; i++) {
            args.pushBack(argv[i]);
        }

        args.pushBack(nullptr);

        pid_t pid = fork();

        if (pid != 0) {
            return pid > 0;
        }

        resetSignals();

        if (dup2(pipe, 3) < 0 || fcntl(3, F_SETFD, 0) < 0) {
            _exit(126);
        }

        execv(args[0], args.mutData());
        _exit(127);
    }

    void spawnApp(char** args, char** env, int passFd) {
        pid_t pid = fork();

        if (pid != 0) {
            if (passFd >= 0) {
                close(passFd);
            }

            return;
        }

        resetSignals();

        int nullFd = open("/dev/null", O_RDWR | O_CLOEXEC);

        if (nullFd < 0 || dup2(nullFd, STDIN_FILENO) < 0 || dup2(nullFd, STDOUT_FILENO) < 0 || dup2(nullFd, STDERR_FILENO) < 0) {
            _exit(126);
        }

        // the attached fd becomes fd 3 in the child, cloexec cleared by dup2
        if (passFd >= 0 && passFd != 3 && dup2(passFd, 3) < 0) {
            _exit(126);
        }

        if (passFd == 3 && fcntl(3, F_SETFD, 0) < 0) {
            _exit(126);
        }

        for (char** value = env; *value; value++) {
            putenv(*value);
        }

        execvp(args[0], args);
        _exit(127);
    }

    bool takeStrings(char*& cursor, char* end, char** out, size_t count) {
        for (size_t i = 0; i < count; i++) {
            char* zero = (char*)memchr(cursor, 0, (size_t)(end - cursor));

            if (!zero) {
                return false;
            }

            out[i] = cursor;
            cursor = zero + 1;
        }

        out[count] = nullptr;
        return true;
    }

    bool parseRequest(u8* packet, size_t size, char** args, char** env) {
        if (size < sizeof(SpawnRequest)) {
            return false;
        }

        SpawnRequest request;

        memcpy(&request, packet, sizeof(request));

        if (!request.argCount || request.argCount > 1024 || request.envCount > 1024) {
            return false;
        }

        char* cursor = (char*)packet + sizeof(request);
        char* end = (char*)packet + size;

        if (!takeStrings(cursor, end, args, request.argCount) || !takeStrings(cursor, end, env, request.envCount) || cursor != end || !*args[0]) {
            return false;
        }

        for (size_t i = 0; i < request.envCount; i++) {
            if (!strchr(env[i], '=')) {
                return false;
            }
        }

        return true;
    }

    // one seqpacket datagram = one request; an attached SCM_RIGHTS fd (if
    // any) lands in *fd. Returns the packet size, 0 on EOF or a dead peer.
    size_t recvPacket(int sock, void* data, size_t cap, int* fd) {
        *fd = -1;

        iovec io = {data, cap};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))];
        msghdr msg{};

        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        ssize_t count;

        do {
            count = recvmsg(sock, &msg, MSG_CMSG_CLOEXEC);
        } while (count < 0 && errno == EINTR);

        if (count <= 0) {
            return 0;
        }

        for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS && c->cmsg_len == CMSG_LEN(sizeof(int))) {
                memcpy(fd, CMSG_DATA(c), sizeof(int));
            }
        }

        return (size_t)count;
    }

    void sendPacket(int sock, const void* data, size_t size, int fd) {
        iovec io = {(void*)data, size};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))];
        msghdr msg{};

        msg.msg_iov = &io;
        msg.msg_iovlen = 1;

        if (fd >= 0) {
            msg.msg_control = control;
            msg.msg_controllen = CMSG_SPACE(sizeof(int));

            cmsghdr* c = CMSG_FIRSTHDR(&msg);

            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            c->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(c), &fd, sizeof(int));
        }

        ssize_t count;

        do {
            count = sendmsg(sock, &msg, 0);
        } while (count < 0 && errno == EINTR);
    }
}

void SupervisorImpl::spawn(const SupervisorSpawn& spec) {
    if (!spec.args || !spec.argCount || spec.argCount > 1024 || spec.envCount > 1024) {
        return;
    }

    SpawnRequest request = {
        .size = 0,
        .argCount = (u32)spec.argCount,
        .envCount = (u32)spec.envCount,
    };
    Buffer packet(sizeof(request));

    packet.append(&request, sizeof(request));

    char zero = 0;

    for (size_t i = 0; i < spec.argCount; i++) {
        packet.append(spec.args[i].data(), spec.args[i].length());
        packet.append(&zero, 1);
    }

    for (size_t i = 0; i < spec.envCount; i++) {
        packet.append(spec.env[i].data(), spec.env[i].length());
        packet.append(&zero, 1);
    }

    if (packet.used() > maxPacket) {
        return;
    }

    ((SpawnRequest*)packet.mutData())->size = (u32)packet.used();
    sendPacket(3, packet.data(), packet.used(), spec.fd);
}

Supervisor* Supervisor::create(Composer& c) {
    return c.pool->make<SupervisorImpl>();
}

int mainSupervisor(int argc, char** argv) {
    if (getpgrp() != getpid() && setpgid(0, 0) != 0) {
        return 1;
    }

    processGroup = (sig_atomic_t)getpgrp();

    if (!installSignals()) {
        return 1;
    }

    // seqpacket keeps request boundaries and carries SCM_RIGHTS atomically
    // with its packet
    int pipes[2];

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pipes) != 0) {
        return 1;
    }

    if (!startComposer(argc, argv, pipes[1])) {
        close(pipes[0]);
        close(pipes[1]);
        return 1;
    }

    close(pipes[1]);

    for (;;) {
        u8 packet[maxPacket];
        int fd = -1;
        size_t got = recvPacket(pipes[0], packet, sizeof(packet), &fd);

        if (!got) {
            finish(0);
        }

        SpawnRequest request;

        if (got < sizeof(request)) {
            finish(1);
        }

        memcpy(&request, packet, sizeof(request));

        if (request.size != got) {
            finish(1);
        }

        char* args[1025];
        char* env[1025];

        if (parseRequest(packet, request.size, args, env)) {
            spawnApp(args, env, fd);
        } else if (fd >= 0) {
            close(fd);
        }
    }
}
