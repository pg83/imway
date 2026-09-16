#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

#include <stddef.h>

struct Composer;

struct SpawnSpec {
    const stl::StringView* args = nullptr;
    size_t argCount = 0;
    const stl::StringView* env = nullptr; // KEY=VALUE entries, on top of the compositor's own
    size_t envCount = 0;
    // handed to the child as fd 3; the caller still owns and closes its copy
    int fd = -1;
};

// The compositor starts its children itself and reaps them from the event
// loop. Everything an exec needs — the resolved executable, argv, envp, the
// /dev/null fd for the child's stdio — is prepared before fork: the child
// is a copy of the forking thread alone, so any lock another thread (the
// offload lane, the driver's workers) holds at that instant stays locked in
// the child forever. Between fork and exec only async-signal-safe calls run.
struct Spawner {
    virtual void spawn(const SpawnSpec& spec) = 0;
    virtual ~Spawner() noexcept = default;

    static Spawner* create(Composer& c);
};
