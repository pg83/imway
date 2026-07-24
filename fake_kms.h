#pragma once

// test build only: a userspace KMS emulator behind a plain fd. The returned
// fd is a pipe end; a libc-level ioctl/fstat interposer (also in
// fake_kms.cpp, linked only into the test binary) routes DRM ioctls on it
// into the emulator and forwards everything else to the kernel untouched.
int fakeKmsOpenDevice();

// fault injection, driven by control verbs
bool fakeKmsActive();
void fakeKmsSetConnected(bool connected);
// swap the connector's mode list, modeling a different display being
// plugged in: the tv set offers only 1920x1080, and commits with a mode
// the current list does not carry are refused
void fakeKmsSetTvModes(bool tv);
// the next atomic commits fail with err until count runs out
void fakeKmsFailCommits(int err, int count);
// commits flipping a framebuffer never flipped before fail with err: the
// compositor's own swapchain is grandfathered in at boot, so this hits
// exactly the next direct-scanout attempt of a client buffer
void fakeKmsFailNewFb(int err);
// after skip more imports pass, the next prime fd imports fail with err
// until count runs out: skip rides over the wayland-side importability
// probe that fires when the client creates its buffer
void fakeKmsFailPrime(int err, int count, int skip);
// the next AddFB2 calls fail with err until count runs out
void fakeKmsFailAddFb(int err, int count);
// commits that put a nonzero framebuffer on the cursor plane fail with
// err; shut-off cursor props still pass, so a modeset without the cursor
// survives — the shape of a display that cannot do hardware cursors
void fakeKmsRejectCursor(int err);
// page-flip events delivered so far: the ground truth for "a frame made
// it to the screen", independent of the compositor's own counters
unsigned long long fakeKmsFlips();
