#pragma once

struct Composer;

struct Session {
    virtual int openDevice(const char* path) = 0;
    virtual void closeDevice(int fd) = 0;

    static Session* create(Composer& c);
    static Session* createDirect(Composer& c);
};
