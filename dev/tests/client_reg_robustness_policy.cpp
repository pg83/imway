#include "robustness.h"

#include <errno.h>
#include <stdio.h>

int main() {
    RestartBackoff backoff;
    const unsigned expected[] = {
        100,
        150,
        225,
        338,
        507,
        761,
        1142,
        1713,
        2570,
        3855,
        5783,
        8675,
        10000,
        10000,
    };

    for (unsigned value : expected) {
        unsigned got = backoff.next(0);

        if (got != value) {
            fprintf(stderr, "backoff: got %u, want %u\n", got, value);
            return 1;
        }
    }

    if (backoff.next(60000) != 100 || backoff.next(0) != 150) {
        fprintf(stderr, "backoff did not reset after a stable run\n");
        return 1;
    }

    const int transient[] = {EBUSY, EAGAIN, EINTR, ENOMEM};

    for (int error : transient) {
        if (!transientScanoutError(error)) {
            fprintf(stderr, "errno %d should be transient\n", error);
            return 1;
        }
    }

    if (transientScanoutError(EINVAL) || transientScanoutError(ENOSPC) || transientScanoutError(0)) {
        fprintf(stderr, "permanent KMS failure classified as transient\n");
        return 1;
    }

    return 0;
}
