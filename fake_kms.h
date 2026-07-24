#pragma once

// test build only: a userspace KMS emulator behind a plain fd. The returned
// fd is a pipe end; a libc-level ioctl/fstat interposer (also in
// fake_kms.cpp, linked only into the test binary) routes DRM ioctls on it
// into the emulator and forwards everything else to the kernel untouched.
int fakeKmsOpenDevice();

// fault injection, driven by control verbs
bool fakeKmsActive();
void fakeKmsSetConnected(bool connected);
// the next atomic commits fail with err until count runs out
void fakeKmsFailCommits(int err, int count);
// commits flipping a framebuffer never flipped before fail with err: the
// compositor's own swapchain is grandfathered in at boot, so this hits
// exactly the next direct-scanout attempt of a client buffer
void fakeKmsFailNewFb(int err);
