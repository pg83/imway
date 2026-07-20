#include "main_supervisor.h"

#include "composer.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
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

        return sigaction(SIGCHLD, &action, nullptr) == 0 &&
               sigaction(SIGPIPE, &action, nullptr) == 0;
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

    void spawnApp(char** args, char** env) {
        if (fork() != 0) {
            return;
        }

        resetSignals();

        int nullFd = open("/dev/null", O_RDWR | O_CLOEXEC);

        if (nullFd < 0 || dup2(nullFd, STDIN_FILENO) < 0 ||
            dup2(nullFd, STDOUT_FILENO) < 0 || dup2(nullFd, STDERR_FILENO) < 0) {
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

        if (!request.argCount || request.argCount > 1024 ||
            request.envCount > 1024) {
            return false;
        }

        char* cursor = (char*)packet + sizeof(request);
        char* end = (char*)packet + size;

        if (!takeStrings(cursor, end, args, request.argCount) ||
            !takeStrings(cursor, end, env, request.envCount) || cursor != end ||
            !*args[0]) {
            return false;
        }

        for (size_t i = 0; i < request.envCount; i++) {
            if (!strchr(env[i], '=')) {
                return false;
            }
        }

        return true;
    }

    size_t readAll(int fd, void* data, size_t size) {
        size_t done = 0;

        while (done < size) {
            ssize_t count = read(fd, (u8*)data + done, size - done);

            if (count > 0) {
                done += (size_t)count;
            } else if (count == 0) {
                break;
            } else if (errno != EINTR) {
                return 0;
            }
        }

        return done;
    }

    void writeAll(int fd, const void* data, size_t size) {
        const u8* cursor = (const u8*)data;

        while (size) {
            ssize_t count = write(fd, cursor, size);

            if (count > 0) {
                cursor += count;
                size -= (size_t)count;
            } else if (count < 0 && errno != EINTR) {
                return;
            }
        }
    }
}

void SupervisorImpl::spawn(const SupervisorSpawn& spec) {
    if (!spec.args || !spec.argCount || spec.argCount > 1024 ||
        spec.envCount > 1024) {
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
    writeAll(3, packet.data(), packet.used());
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

    int pipes[2];

    if (pipe2(pipes, O_CLOEXEC) != 0) {
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
        SpawnRequest request;
        size_t got = readAll(pipes[0], &request, sizeof(request));

        if (!got) {
            finish(0);
        }
        if (got != sizeof(request) || request.size < sizeof(request) ||
            request.size > sizeof(packet)) {
            finish(1);
        }

        memcpy(packet, &request, sizeof(request));

        if (readAll(pipes[0], packet + sizeof(request),
                    request.size - sizeof(request)) != request.size - sizeof(request)) {
            finish(1);
        }

        char* args[1025];
        char* env[1025];

        if (parseRequest(packet, request.size, args, env)) {
            spawnApp(args, env);
        }
    }
}
