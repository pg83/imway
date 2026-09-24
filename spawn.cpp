#include "spawn.h"

#include "log.h"
#include "coverage.h"
#include "chaos_monkey.h"
#include "util.h"
#include "composer.h"
#include "ev_watch.h"

#include <std/ios/manip.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ev.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

extern char** environ;

using namespace stl;

#ifdef IMWAY_FOR_TESTS
// the coverage runtime's writer, present only in an instrumented build
#endif

namespace {
    struct SpawnerImpl: Spawner {
        SpawnerImpl(Composer& c);
        ~SpawnerImpl() noexcept override;

        void spawn(const SpawnSpec& spec) override;

        Composer* comp = nullptr;
        // stdio of every child; opened once, close-on-exec
        int nullFd = -1;
        // where a child's stdout and stderr go: /dev/null, or the file
        // IMWAY_CHILD_LOG names in the test build
        int childOut = -1;
        ev_child children{};
    };

    static void childCb(struct ev_loop*, ev_child* w, int) {
        auto* self = (SpawnerImpl*)w->data;
        Log& log = *self->comp->log;

        if (WIFEXITED(w->rstatus)) {
            log << "imway: child "_sv << (long)w->rpid << " exited with status "_sv << (long)WEXITSTATUS(w->rstatus) << endL;
        } else {
            // a watcher that does not trace hears of terminations only
            log << "imway: child "_sv << (long)w->rpid << " killed by signal "_sv << (long)WTERMSIG(w->rstatus) << endL;
        }
    }

    // the executable is looked up by the parent, so the child needs no
    // PATH walk of its own; an absent command is refused before fork
    static bool resolveExecutable(StringView name, Buffer& out) {
        out.append(name.data(), name.length());

        if (memchr(name.data(), '/', name.length())) {
            out.cStr();

            return true;
        }

        const char* path = getenv("PATH");

        if (!path) {
            path = "/usr/local/bin:/usr/bin:/bin";
        }

        StringView rest(path);

        while (!rest.empty()) {
            StringView dir, tail;

            if (rest.split(':', dir, tail)) {
                rest = tail;
            } else {
                dir = rest;
                rest = {};
            }

            Buffer candidate;

            if (!dir.empty()) {
                candidate.append(dir.data(), dir.length());
                candidate.append("/", 1);
            }

            candidate.append(name.data(), name.length());

            if (access(candidate.cStr(), X_OK) == 0) {
                out = (Buffer&&)candidate;
                out.cStr();

                return true;
            }
        }

        return false;
    }

    // keyValue is a KEY=VALUE entry, as SpawnSpec requires
    static bool sameKey(const char* entry, StringView keyValue) {
        const char* key = (const char*)keyValue.data();
        size_t keyLen = (size_t)((const char*)memchr(key, '=', keyValue.length()) - key);

        return strncmp(entry, key, keyLen) == 0 && entry[keyLen] == '=';
    }

    // every descriptor is close-on-exec by construction (see the audit in
    // the spawn scenario); this is the belt to those braces, so a library
    // that slipped one past still cannot leak it into the child
    static void closeFrom(int first) {
#ifdef SYS_close_range
        if (syscall(SYS_close_range, (unsigned)first, ~0u, 0) == 0 || errno != ENOSYS) {
            return;
        }
#endif

        struct rlimit limit{};
        int last = 65536;

        if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur < (rlim_t)last) {
            last = (int)limit.rlim_cur;
        }

        for (int fd = first; fd < last; fd++) {
            close(fd);
        }
    }

    // Nothing here may allocate, lock or log: the child owns a copy of one
    // thread and none of the locks the others held at fork.
    [[noreturn]] static void execChild(pid_t parent, int nullFd, int outFd, int passFd, const char* path, char** argv, char** envp) {
        sigset_t mask;

        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, nullptr);

        for (int sig = 1; sig < NSIG; sig++) {
            signal(sig, SIG_DFL);
        }

        // children do not outlive the compositor; the parent check closes
        // the window in which it already died before the prctl took effect
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent) {
            _exit(126);
        }

        if (dup2(nullFd, STDIN_FILENO) < 0 || dup2(outFd, STDOUT_FILENO) < 0 || dup2(outFd, STDERR_FILENO) < 0) {
            _exit(126);
        }

        // the attached fd becomes fd 3, its close-on-exec cleared by dup2 or
        // explicitly when it already sits there
        if (passFd >= 0 && passFd != 3 && dup2(passFd, 3) < 0) {
            _exit(126);
        }

        if (passFd == 3 && fcntl(3, F_SETFD, 0) < 0) {
            _exit(126);
        }

        closeFrom(passFd >= 0 ? 4 : 3);
#ifdef IMWAY_FOR_TESTS
        // exec replaces the image before the coverage runtime's exit hook
        // runs: what the child did up to here is written out first, in a
        // file of its own pid
        flushCoverage();
#endif
        execve(path, argv, envp);
#ifdef IMWAY_FOR_TESTS
        flushCoverage();
#endif
        _exit(127);
    }
}

SpawnerImpl::SpawnerImpl(Composer& c)
    : comp(&c)
{
    nullFd = comp->chaos->devNull(open("/dev/null", O_RDWR | O_CLOEXEC));

    if (nullFd < 0) {
        *comp->log << "imway: spawn: /dev/null unavailable: "_sv << StringView(strerror(errno)) << endL;
    }

    childOut = nullFd;

#ifdef IMWAY_FOR_TESTS
    if (const char* path = getenv("IMWAY_CHILD_LOG"); path && *path) {
        int log = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

        if (log >= 0) {
            childOut = log;
        }
    }
#endif

    evChildInit(&children, childCb, 0, 0);
    children.data = this;
    ev_child_start(comp->loop, &children);
    // reaping must not keep the loop alive by itself
    ev_unref(comp->loop);
}

SpawnerImpl::~SpawnerImpl() noexcept {
    ev_ref(comp->loop);
    ev_child_stop(comp->loop, &children);

    if (childOut >= 0 && childOut != nullFd) {
        close(childOut);
    }

    if (nullFd >= 0) {
        close(nullFd);
    }
}

void SpawnerImpl::spawn(const SpawnSpec& spec) {
    // every caller builds its spec from literals: an argv of three or five
    // words and a handful of KEY=VALUE entries
    if (nullFd < 0) {
        return;
    }

    Buffer path;

    if (!resolveExecutable(spec.args[0], path)) {
        *comp->log << "imway: spawn: "_sv << spec.args[0] << " not found"_sv << endL;

        return;
    }

    // every string lands in one buffer first; the pointer arrays are built
    // afterwards, when the buffer no longer moves
    Buffer strings;
    Vector<size_t> offsets(spec.argCount + spec.envCount);
    char zero = 0;

    for (size_t i = 0; i < spec.argCount; i++) {
        offsets.pushBack(strings.used());
        strings.append(spec.args[i].data(), spec.args[i].length());
        strings.append(&zero, 1);
    }

    for (size_t i = 0; i < spec.envCount; i++) {
        offsets.pushBack(strings.used());
        strings.append(spec.env[i].data(), spec.env[i].length());
        strings.append(&zero, 1);
    }

    char* base = (char*)strings.mutData();
    Vector<char*> argv(spec.argCount + 1);
    Vector<char*> envp;

    for (size_t i = 0; i < spec.argCount; i++) {
        argv.pushBack(base + offsets[i]);
    }

    argv.pushBack(nullptr);

    // the compositor's environment, with the given entries replacing their
    // namesakes
    for (char** entry = environ; entry && *entry; entry++) {
        bool overridden = false;

        for (size_t i = 0; i < spec.envCount && !overridden; i++) {
            overridden = sameKey(*entry, spec.env[i]);
        }

        if (!overridden) {
            envp.pushBack(*entry);
        }
    }

    for (size_t i = 0; i < spec.envCount; i++) {
        envp.pushBack(base + offsets[spec.argCount + i]);
    }

    envp.pushBack(nullptr);

    pid_t parent = getpid();
    pid_t pid = fork();

    if (pid < 0) {
        *comp->log << "imway: spawn: fork failed: "_sv << StringView(strerror(errno)) << endL;

        return;
    }

    if (pid == 0) {
        execChild(parent, nullFd, childOut, spec.fd, path.cStr(), argv.mutData(), envp.mutData());
    }

    *comp->log << "imway: spawned "_sv << (long)pid << ": "_sv << sv(path) << endL;
}

Spawner* Spawner::create(Composer& c) {
    return c.pool->make<SpawnerImpl>(c);
}
