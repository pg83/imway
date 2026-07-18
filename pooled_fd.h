#pragma once

namespace stl {
    class ObjPool;
}

struct Session;

// close(fd) at pool death
int* pooledFD(stl::ObjPool& pool, int fd);

// a device fd opened through the libseat session: goes back via
// Session::closeDevice, not close()
void pooledSessionFD(stl::ObjPool& pool, Session& session, int fd);
