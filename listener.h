#pragma once

#include <std/lib/node.h>

struct Listener: stl::IntrusiveNode {
    virtual void onListen() = 0;
    ~Listener() noexcept;
};
