#pragma once

struct Output {
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;

    virtual bool start() = 0;
    virtual void present(const void* pixels) = 0;
};
