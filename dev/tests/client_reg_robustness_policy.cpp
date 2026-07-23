#include "robustness.h"

#include <errno.h>
#include <stdio.h>

int main() {
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
