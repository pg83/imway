#pragma once

#include <stddef.h>

#include <std/str/view.h>
#include <std/sys/types.h>

struct Composer;

struct SupervisorSpawn {
    const stl::StringView* args = nullptr;
    size_t argCount = 0;
    const stl::StringView* env = nullptr; // KEY=VALUE entries
    size_t envCount = 0;
    int passFd = -1;
    int targetFd = -1;
    bool exitWithChild = false;
};

struct Supervisor {
    virtual i32 spawn(const SupervisorSpawn& spec) = 0;
    virtual ~Supervisor() noexcept = default;

    static Supervisor* create(Composer& c);
};

int mainSupervisor(int argc, char** argv);
