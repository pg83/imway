#pragma once

namespace stl {
    class StringView;
}

// A userspace stand-in for the KMS device, handed to the KMS backend by
// the composition root. openDevice() yields the fd the backend drives;
// the test binary's libc-level overrides route that fd's ioctls into the
// emulator, and every other fd passes through untouched. The rest is
// fault scripting for scenarios, wired to control verbs.
struct KmsIntercept {
    virtual int openDevice() = 0;

    // connector hotplug: flip the link and re-probe via Output::hotplug();
    // 0 unplugged, 1 plugged, 2 the connector object itself gone for the
    // moment (an MST port going away under the probe)
    virtual void setConnected(int state) = 0;
    // the connector's mode list: 0 default, 1 tv (1080p only), 2 small
    // (800p only), 3 a 1366x768 panel whose dumb buffers need padded rows
    // — a swapped display without touching the link
    virtual void setModes(int set) = 0;
    // the next atomic commits fail with err until count runs out; testToo
    // extends that to TEST_ONLY commits — the shape of losing drm master
    // (vt switch away), where every atomic ioctl bounces, not just flips
    virtual void failCommits(int err, int count, bool testToo) = 0;
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
    // the non-desktop connector's lease path breaks: 1 the connector is
    // gone, 2 its encoder is, 3 the encoder reaches only the desktop crtc,
    // 4 the kernel refuses the lease; 0 heals it
    virtual void leaseFault(int kind) = 0;
    // lookups the driver answers with an error, as comma-separated
    // kind:target:skip:count rules (count -1: for good): props:<object id>,
    // prop:<name>, blob:<name of the property holding it>, plane:<id>,
    // resources, createblob (creating any property blob), planes (the plane
    // list), encoder:<id>, connector:<id>, clientcap (setting a client
    // capability), mapdumb (mapping a dumb buffer). Each rule lets skip
    // matching lookups through first; an empty list clears them
    virtual void failLookups(stl::StringView rules) = 0;
    // the connector's DDC/CI bus, the i2c node the sysfs walk found: the
    // fd of an emulated monitor, or -errno when none answers on it
    virtual int openDdc(stl::StringView bus) = 0;
    // page-flip events delivered so far: the ground truth for "a frame
    // made it to the screen", independent of the compositor's counters
    virtual unsigned long long flips() = 0;
};
