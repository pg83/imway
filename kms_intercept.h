#pragma once

// A userspace stand-in for the KMS device, handed to the KMS backend by
// the composition root. openDevice() yields the fd the backend drives;
// the test binary's libc-level overrides route that fd's ioctls into the
// emulator, and every other fd passes through untouched. The rest is
// fault scripting for scenarios, wired to control verbs.
struct KmsIntercept {
    virtual int openDevice() = 0;

    // connector hotplug: flip the link and re-probe via Output::hotplug()
    virtual void setConnected(bool connected) = 0;
    // the connector's mode list: 0 default, 1 tv (1080p only), 2 small
    // (800p only) — a swapped display without touching the link
    virtual void setModes(int set) = 0;
    // the next atomic commits fail with err until count runs out
    virtual void failCommits(int err, int count) = 0;
    // commits flipping a framebuffer created after this call fail with err:
    // the compositor's swapchain predates it, so this hits exactly the next
    // direct-scanout attempt of a client buffer
    virtual void failNewFb(int err) = 0;
    // after skip more imports pass, prime fd imports fail with err until
    // count runs out: skip rides over the buffer-create importability probe
    virtual void failPrime(int err, int count, int skip) = 0;
    // the next AddFB2 calls fail with err until count runs out
    virtual void failAddFb(int err, int count) = 0;
    // commits that put a nonzero framebuffer on the cursor plane fail with
    // err; shut-off cursor props still pass — a display that cannot do
    // hardware cursors
    virtual void rejectCursor(int err) = 0;
    // page-flip events delivered so far: the ground truth for "a frame
    // made it to the screen", independent of the compositor's counters
    virtual unsigned long long flips() = 0;
};
