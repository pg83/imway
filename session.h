#pragma once

#include <std/lib/node.h>
#include <std/str/view.h>

struct Composer;

struct SessionListener: stl::IntrusiveNode {
    virtual void sessionEnabled() = 0;
    virtual void sessionDisabled() = 0;
};

struct Session {
    virtual stl::StringView seatName() const = 0;

    virtual int openDevice(const char* path) = 0;
    virtual void closeDevice(int fd) = 0;

    static Session* create(Composer& c);
    static Session* createDirect(Composer& c);
};
