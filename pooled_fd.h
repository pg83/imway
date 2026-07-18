#pragma once

namespace stl {
    class ObjPool;
}

struct Session;

// close(fd) at pool death
void pooledFD(stl::ObjPool& pool, int fd);

// a device fd opened through the libseat session: goes back via
// Session::closeDevice, not close()
void pooledSessionFD(stl::ObjPool& pool, Session& session, int fd);

// an fd slot for owners that reopen: reset closes the current fd and takes
// over the next
struct PooledFD {
    virtual int get() const = 0;
    virtual void reset(int newFd) = 0;

    static PooledFD* create(stl::ObjPool& pool, int fd);
};
