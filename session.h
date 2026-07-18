#pragma once

#include <std/str/view.h>

struct Composer;

struct Session {
    virtual stl::StringView seatName() const = 0;

    virtual int openDevice(const char* path) = 0;
    virtual void closeDevice(int fd) = 0;

    static Session* create(Composer& c);
    static Session* createDirect(Composer& c);
};
