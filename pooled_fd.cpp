#include "pooled_fd.h"

#include "session.h"

#include <std/mem/obj_pool.h>

#include <unistd.h>

using namespace stl;

namespace {
    struct FDBox {
        int fd = -1;

        FDBox(int f);
        ~FDBox() noexcept;
    };

    struct SessionFDBox {
        Session* session = nullptr;
        int fd = -1;

        SessionFDBox(Session& s, int f);
        ~SessionFDBox() noexcept;
    };
}

FDBox::FDBox(int f)
    : fd(f)
{
}

FDBox::~FDBox() noexcept {
    if (fd >= 0) {
        close(fd);
    }
}

int* pooledFD(ObjPool& pool, int fd) {
    return &pool.make<FDBox>(fd)->fd;
}

SessionFDBox::SessionFDBox(Session& s, int f)
    : session(&s)
    , fd(f)
{
}

SessionFDBox::~SessionFDBox() noexcept {
    if (fd >= 0) {
        session->closeDevice(fd);
    }
}

void pooledSessionFD(ObjPool& pool, Session& session, int fd) {
    pool.make<SessionFDBox>(session, fd);
}
