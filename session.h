#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct SessionListener {
    virtual void sessionEnabled() = 0;
    virtual void sessionDisabled() = 0;
};

struct Session {
    virtual stl::StringView seatName() const = 0;

    virtual int openDevice(const char* path) = 0;
    virtual void closeDevice(int fd) = 0;

    virtual void addListener(SessionListener*) = 0;

    static Session* create(stl::ObjPool* pool, struct ev_loop* loop);
    static Session* createDirect(stl::ObjPool* pool);
};
