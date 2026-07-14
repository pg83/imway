#pragma once

struct ScanoutBuffer;

struct Output {
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;

    virtual bool start() = 0;

    virtual bool ready() const = 0;
    virtual bool vsynced() const = 0;

    virtual int scanoutCount() const = 0;
    virtual ScanoutBuffer* scanoutBuffer(int i) = 0;
    virtual int acquire() = 0;
    virtual void presentImage(int i) = 0;

    virtual bool presentNeedsPixels() const = 0;
    virtual void present(const void* pixels) = 0;
};
