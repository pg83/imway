#pragma once

struct Composer;
struct Listener;

// One background job on the composer's offload thread with a loop-side
// completion: run() executes work(self) on the worker and done fires on
// the event loop after the pass retires. A run() while a pass is in
// flight coalesces — the work runs once more after the current pass, so
// event bursts fold into one final re-run. Per-run data travels through
// the owner's fields: inputs written on the loop before run(), results
// read back in the listener; the eventfd hop orders both directions.
struct OffloadJob {
    virtual void run() = 0;
    virtual bool inFlight() const = 0;
    // block until the pass in flight retires (teardown)
    virtual void join() = 0;

    static OffloadJob* create(Composer& c, void (*work)(void*), void* self, Listener& done);
};
