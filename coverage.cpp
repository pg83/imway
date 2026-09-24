#include "coverage.h"

// present only in coverage-instrumented builds
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));

void flushCoverage() {
    if (__llvm_profile_write_file) {
        __llvm_profile_write_file();
    }
}
