#pragma once

#include <std/lib/node.h>

struct Listener: stl::IntrusiveNode {
    virtual void onListen(void* arg) = 0;

    void onListen() {
        onListen(nullptr);
    }

    ~Listener() noexcept;
};
