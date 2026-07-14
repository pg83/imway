#include "util.h"

#include <time.h>

u32 nowMsec() {
    timespec ts{};

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
