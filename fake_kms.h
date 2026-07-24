#pragma once

struct KmsIntercept;

// test build only: returns the userspace KMS emulator's face. The libc
// ioctl/fstat/mmap overrides linked into the test binary stay transparent
// until the returned interceptor's openDevice() creates the fake fd; a
// binary that never calls this runs entirely past the emulator.
KmsIntercept* installInterceptor();
