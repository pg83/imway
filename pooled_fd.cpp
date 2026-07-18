#include "pooled_fd.h"

#include "session.h"

#include <unistd.h>

#include <std/mem/obj_pool.h>

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

    struct PooledFDImpl: public PooledFD {
        int fd = -1;

        PooledFDImpl(int f);
        ~PooledFDImpl() noexcept;

        int get() const override;
        void reset(int newFd) override;
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

void pooledFD(ObjPool& pool, int fd) {
    pool.make<FDBox>(fd);
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

PooledFDImpl::PooledFDImpl(int f)
    : fd(f)
{
}

PooledFDImpl::~PooledFDImpl() noexcept {
    reset(-1);
}

int PooledFDImpl::get() const {
    return fd;
}

void PooledFDImpl::reset(int newFd) {
    if (fd >= 0) {
        close(fd);
    }

    fd = newFd;
}

PooledFD* PooledFD::create(ObjPool& pool, int fd) {
    return pool.make<PooledFDImpl>(fd);
}
