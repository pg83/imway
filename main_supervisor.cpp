#include "main_supervisor.h"

#include "composer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ev.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr size_t kMaxPacket = 64 * 1024;

    enum class Op: u32 {
        spawn = 1,
        spawnResult = 2,
        childExited = 3,
    };

    enum SpawnFlags: u32 {
        spawnHasFd = 1u << 0,
    };

    struct Header {
        Op op{};
        u32 size = 0;
        u64 requestId = 0;
    };

    struct SpawnPacket {
        Header header;
        u32 flags = 0;
        u32 argCount = 0;
        u32 envCount = 0;
        i32 targetFd = -1;
    };

    struct SpawnResultPacket {
        Header header;
        i32 error = 0;
        i32 pid = -1;
    };

    struct ChildExitedPacket {
        Header header;
        i32 pid = -1;
        i32 status = 0;
    };

    struct Child {
        pid_t pid = -1;
    };

    struct SupervisorImpl: Supervisor {
        Composer* c = nullptr;
        int fd = STDIN_FILENO;
        ev_io io{};
        u64 nextRequest = 1;
        pid_t exitPid = -1;

        SupervisorImpl(Composer& comp);
        ~SupervisorImpl() noexcept;

        i32 spawn(const SupervisorSpawn& spec) override;
        void received(const void* packet, size_t size);
        void disconnected();
    };

    bool appendBytes(u8* packet, size_t& size, const void* data, size_t n) {
        if (n > kMaxPacket - size) {
            return false;
        }

        memcpy(packet + size, data, n);
        size += n;

        return true;
    }

    bool appendString(u8* packet, size_t& size, StringView value) {
        if (value.length() > UINT32_MAX) {
            return false;
        }

        u32 length = (u32)value.length();

        return appendBytes(packet, size, &length, sizeof(length)) &&
               appendBytes(packet, size, value.data(), value.length());
    }

    bool takeBytes(const u8* packet, size_t size, size_t& off, void* out, size_t n) {
        if (n > size - off) {
            return false;
        }

        memcpy(out, packet + off, n);
        off += n;

        return true;
    }

    char* takeString(const u8* packet, size_t size, size_t& off) {
        u32 length = 0;

        if (!takeBytes(packet, size, off, &length, sizeof(length)) || length > size - off) {
            return nullptr;
        }

        char* out = (char*)malloc((size_t)length + 1);

        if (!out) {
            return nullptr;
        }

        memcpy(out, packet + off, length);
        out[length] = 0;
        off += length;

        return out;
    }

    ssize_t sendPacket(int fd, const void* packet, size_t size, int passFd = -1) {
        iovec vec{(void*)packet, size};
        msghdr msg{};
        u8 control[CMSG_SPACE(sizeof(int))] = {};

        msg.msg_iov = &vec;
        msg.msg_iovlen = 1;

        if (passFd >= 0) {
            msg.msg_control = control;
            msg.msg_controllen = sizeof(control);

            cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);

            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(cmsg), &passFd, sizeof(passFd));
        }

        ssize_t n;

        do {
            n = sendmsg(fd, &msg, MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);

        return n;
    }

    ssize_t receivePacket(int fd, void* packet, size_t size, int flags, int& receivedFd) {
        iovec vec{packet, size};
        msghdr msg{};
        u8 control[CMSG_SPACE(sizeof(int))] = {};

        msg.msg_iov = &vec;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        receivedFd = -1;

        ssize_t n;

        do {
            n = recvmsg(fd, &msg, flags | MSG_CMSG_CLOEXEC);
        } while (n < 0 && errno == EINTR);

        if (n <= 0 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
            return n > 0 ? -EMSGSIZE : n;
        }

        for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
                cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
                memcpy(&receivedFd, CMSG_DATA(cmsg), sizeof(receivedFd));

                break;
            }
        }

        return n;
    }

    void ioCb(struct ev_loop*, ev_io* w, int) {
        auto* self = (SupervisorImpl*)w->data;
        u8 packet[kMaxPacket];

        for (;;) {
            int receivedFd = -1;
            ssize_t n = receivePacket(self->fd, packet, sizeof(packet), MSG_DONTWAIT, receivedFd);

            if (receivedFd >= 0) {
                close(receivedFd); // responses and events never carry descriptors
            }

            if (n > 0) {
                self->received(packet, (size_t)n);

                continue;
            }

            if (n == 0) {
                self->disconnected();
            } else if (n != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                self->disconnected();
            }

            return;
        }
    }

    void resetChildSignals() {
        sigset_t empty;

        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);

        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
    }

    void closeSpan(unsigned first, unsigned last) {
        if (first > last) {
            return;
        }

        if (syscall(SYS_close_range, first, last, 0) == 0) {
            return;
        }

        long maxFd = sysconf(_SC_OPEN_MAX);
        unsigned end = last == UINT_MAX ? (unsigned)(maxFd > 0 ? maxFd : 65536) : last + 1;

        for (unsigned fd = first; fd < end; fd++) {
            close((int)fd);
        }
    }

    void closeForExec(int keepA, int keepB) {
        if (keepA > keepB) {
            int tmp = keepA;
            keepA = keepB;
            keepB = tmp;
        }

        unsigned cursor = 3;

        int keeps[2] = {keepA, keepB};

        for (int keep : keeps) {
            if (keep < 3 || (keep == keepA && keepA == keepB && cursor > (unsigned)keep)) {
                continue;
            }

            if (cursor < (unsigned)keep) {
                closeSpan(cursor, (unsigned)keep - 1);
            }

            cursor = (unsigned)keep + 1;
        }

        closeSpan(cursor, UINT_MAX);
    }

    int launch(char** args, char** env, size_t envCount, int passFd, int targetFd, int controlFd, pid_t& pid) {
        int statusPipe[2];

        if (pipe2(statusPipe, O_CLOEXEC) != 0) {
            return errno;
        }

        pid = fork();

        if (pid < 0) {
            int error = errno;

            close(statusPipe[0]);
            close(statusPipe[1]);

            return error;
        }

        if (pid == 0) {
            resetChildSignals();
            close(statusPipe[0]);

            if (controlFd >= 0) {
                if (dup2(controlFd, STDIN_FILENO) < 0 || dup2(controlFd, STDOUT_FILENO) < 0) {
                    int error = errno;

                    (void)!write(statusPipe[1], &error, sizeof(error));
                    _exit(126);
                }
            }

            int errorFd = targetFd == 3 ? 4 : 3;

            if (passFd >= 0) {
                if (targetFd < 3 || dup2(passFd, targetFd) < 0 || fcntl(targetFd, F_SETFD, 0) < 0) {
                    int error = errno;

                    (void)!write(statusPipe[1], &error, sizeof(error));
                    _exit(126);
                }
            }

            if (dup2(statusPipe[1], errorFd) < 0 || fcntl(errorFd, F_SETFD, FD_CLOEXEC) < 0) {
                int error = errno;

                (void)!write(statusPipe[1], &error, sizeof(error));
                _exit(126);
            }

            closeForExec(errorFd, passFd >= 0 ? targetFd : errorFd);

            for (size_t i = 0; i < envCount; i++) {
                putenv(env[i]);
            }

            execvp(args[0], args);

            int error = errno;

            (void)!write(errorFd, &error, sizeof(error));
            _exit(127);
        }

        close(statusPipe[1]);

        int error = 0;
        ssize_t n;

        do {
            n = read(statusPipe[0], &error, sizeof(error));
        } while (n < 0 && errno == EINTR);

        close(statusPipe[0]);

        if (n == 0) {
            return 0;
        }

        if (n < 0) {
            error = errno;
        } else if (n != sizeof(error)) {
            error = EIO;
        }

        waitpid(pid, nullptr, 0);
        pid = -1;

        return error;
    }

    void freeStrings(char** strings, size_t count) {
        if (!strings) {
            return;
        }

        for (size_t i = 0; i < count; i++) {
            free(strings[i]);
        }

        free(strings);
    }

    bool parseSpawn(const u8* packet, size_t size, char*** argsOut, char*** envOut,
                    size_t& envCount, int& targetFd, bool& wantsFd) {
        if (size < sizeof(SpawnPacket)) {
            return false;
        }

        SpawnPacket wire;

        memcpy(&wire, packet, sizeof(wire));

        if (!wire.argCount || wire.argCount > 1024 || wire.envCount > 1024 ||
            wire.header.size != size) {
            return false;
        }

        char** args = (char**)calloc((size_t)wire.argCount + 1, sizeof(char*));
        char** env = (char**)calloc((size_t)wire.envCount + 1, sizeof(char*));

        if (!args || !env) {
            free(args);
            free(env);

            return false;
        }

        size_t off = sizeof(wire);
        u32 ai = 0, ei = 0;

        for (; ai < wire.argCount; ai++) {
            args[ai] = takeString(packet, size, off);

            if (!args[ai]) {
                break;
            }
        }

        for (; ai == wire.argCount && ei < wire.envCount; ei++) {
            env[ei] = takeString(packet, size, off);

            if (!env[ei] || !strchr(env[ei], '=')) {
                break;
            }
        }

        if (ai != wire.argCount || ei != wire.envCount || off != size) {
            freeStrings(args, wire.argCount);
            freeStrings(env, wire.envCount);

            return false;
        }

        *argsOut = args;
        *envOut = env;
        envCount = wire.envCount;
        targetFd = wire.targetFd;
        wantsFd = (wire.flags & spawnHasFd) != 0;

        return true;
    }

    int exitCode(int status) {
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }

        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }

        return 1;
    }
}

SupervisorImpl::SupervisorImpl(Composer& comp)
    : c(&comp)
{
    ev_io_init(&io, ioCb, fd, EV_READ);
    io.data = this;
    ev_io_start(c->loop, &io);
}

SupervisorImpl::~SupervisorImpl() noexcept {
    ev_io_stop(c->loop, &io);
}

i32 SupervisorImpl::spawn(const SupervisorSpawn& spec) {
    if (!spec.args || !spec.argCount || spec.argCount > 1024 || spec.envCount > 1024 ||
        (spec.passFd >= 0) != (spec.targetFd >= 3)) {
        return -EINVAL;
    }

    u8 packet[kMaxPacket];
    SpawnPacket wire;

    wire.header.op = Op::spawn;
    wire.header.requestId = nextRequest++;
    wire.flags = spec.passFd >= 0 ? spawnHasFd : 0;
    wire.argCount = (u32)spec.argCount;
    wire.envCount = (u32)spec.envCount;
    wire.targetFd = spec.targetFd;

    size_t size = 0;

    if (!appendBytes(packet, size, &wire, sizeof(wire))) {
        return -E2BIG;
    }

    for (size_t i = 0; i < spec.argCount; i++) {
        if (!appendString(packet, size, spec.args[i])) {
            return -E2BIG;
        }
    }

    for (size_t i = 0; i < spec.envCount; i++) {
        if (!appendString(packet, size, spec.env[i])) {
            return -E2BIG;
        }
    }

    ((SpawnPacket*)packet)->header.size = (u32)size;

    if (sendPacket(fd, packet, size, spec.passFd) != (ssize_t)size) {
        return -EPIPE;
    }

    for (;;) {
        int receivedFd = -1;
        ssize_t n = receivePacket(fd, packet, sizeof(packet), 0, receivedFd);

        if (receivedFd >= 0) {
            close(receivedFd);
        }

        if (n <= 0) {
            disconnected();

            return -EPIPE;
        }

        if ((size_t)n < sizeof(Header)) {
            continue;
        }

        Header header;

        memcpy(&header, packet, sizeof(header));

        if (header.op == Op::spawnResult && header.requestId == wire.header.requestId &&
            (size_t)n == sizeof(SpawnResultPacket)) {
            SpawnResultPacket result;

            memcpy(&result, packet, sizeof(result));

            if (result.error) {
                return -result.error;
            }

            if (spec.exitWithChild) {
                exitPid = result.pid;
            }

            return result.pid;
        }

        received(packet, (size_t)n);
    }
}

void SupervisorImpl::received(const void* packet, size_t size) {
    if (size != sizeof(ChildExitedPacket)) {
        return;
    }

    ChildExitedPacket event;

    memcpy(&event, packet, sizeof(event));

    if (event.header.op == Op::childExited && event.pid == exitPid) {
        exitPid = -1;
        ev_break(c->loop, EVBREAK_ALL);
    }
}

void SupervisorImpl::disconnected() {
    ev_io_stop(c->loop, &io);
    ev_break(c->loop, EVBREAK_ALL);
}

Supervisor* Supervisor::create(Composer& c) {
    return c.pool->make<SupervisorImpl>(c);
}

int mainSupervisor(int argc, char** argv) {
    if (getpgrp() != getpid() && setpgid(0, 0) != 0) {
        return 1;
    }

    pid_t processGroup = getpgrp();
    int pair[2];

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) {
        return 1;
    }

    sigset_t signals;

    sigemptyset(&signals);
    sigaddset(&signals, SIGCHLD);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &signals, nullptr) != 0) {
        return 1;
    }

    int signalFd = signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK);

    if (signalFd < 0) {
        return 1;
    }

    char** composerArgs = (char**)calloc((size_t)argc + 2, sizeof(char*));

    if (!composerArgs) {
        return 1;
    }

    composerArgs[0] = (char*)"/proc/self/exe";
    composerArgs[1] = (char*)"composer";

    for (int i = 1; i < argc; i++) {
        composerArgs[i + 1] = argv[i];
    }

    pid_t composerPid = -1;
    int error = launch(composerArgs, nullptr, 0, -1, -1, pair[1], composerPid);

    free(composerArgs);
    close(pair[1]);

    if (error) {
        close(pair[0]);
        close(signalFd);

        return 1;
    }

    Vector<Child> children;

    children.pushBack({composerPid});

    for (;;) {
        pollfd fds[2] = {{pair[0], POLLIN, 0}, {signalFd, POLLIN, 0}};
        int ready;

        do {
            ready = poll(fds, 2, -1);
        } while (ready < 0 && errno == EINTR);

        if (ready < 0) {
            kill(composerPid, SIGTERM);

            continue;
        }

        if (fds[0].revents & POLLIN) {
            u8 packet[kMaxPacket];
            int receivedFd = -1;
            ssize_t n = receivePacket(pair[0], packet, sizeof(packet), 0, receivedFd);

            if (n >= (ssize_t)sizeof(Header)) {
                Header header;

                memcpy(&header, packet, sizeof(header));

                if (header.op == Op::spawn) {
                    char** args = nullptr;
                    char** env = nullptr;
                    size_t envCount = 0;
                    int targetFd = -1;
                    bool wantsFd = false;
                    int spawnError = EPROTO;
                    pid_t pid = -1;

                    if (parseSpawn(packet, (size_t)n, &args, &env, envCount, targetFd, wantsFd)) {
                        if (wantsFd == (receivedFd >= 0)) {
                            spawnError = launch(args, env, envCount, receivedFd, targetFd, -1, pid);

                            if (!spawnError) {
                                children.pushBack({pid});
                            }
                        } else {
                            spawnError = EINVAL;
                        }

                        freeStrings(args, ((SpawnPacket*)packet)->argCount);
                        freeStrings(env, envCount);
                    }

                    if (receivedFd >= 0) {
                        close(receivedFd);
                    }

                    SpawnResultPacket result;

                    result.header.op = Op::spawnResult;
                    result.header.size = sizeof(result);
                    result.header.requestId = header.requestId;
                    result.error = spawnError;
                    result.pid = pid;
                    (void)sendPacket(pair[0], &result, sizeof(result));
                } else if (receivedFd >= 0) {
                    close(receivedFd);
                }
            } else {
                if (receivedFd >= 0) {
                    close(receivedFd);
                }

                if (n <= 0) {
                    kill(composerPid, SIGTERM);
                }
            }
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            kill(composerPid, SIGTERM);
        }

        if (fds[1].revents & POLLIN) {
            signalfd_siginfo info;

            while (read(signalFd, &info, sizeof(info)) == sizeof(info)) {
                if (info.ssi_signo == SIGCHLD) {
                    for (;;) {
                        int status = 0;
                        pid_t pid = waitpid(-1, &status, WNOHANG);

                        if (pid <= 0) {
                            break;
                        }

                        for (size_t i = 0; i < children.length(); i++) {
                            if (children[i].pid == pid) {
                                children.mut(i) = children.back();
                                children.popBack();

                                break;
                            }
                        }

                        if (pid == composerPid) {
                            // Signals are blocked here, so the supervisor can
                            // terminate its own group and still return the
                            // composer's status to IX.
                            kill(-processGroup, SIGTERM);
                            close(pair[0]);
                            close(signalFd);

                            return exitCode(status);
                        }

                        ChildExitedPacket event;

                        event.header.op = Op::childExited;
                        event.header.size = sizeof(event);
                        event.pid = pid;
                        event.status = status;
                        (void)sendPacket(pair[0], &event, sizeof(event));
                    }
                } else {
                    kill(composerPid, (int)info.ssi_signo);
                }
            }
        }
    }
}
