#include "frame_listener.h"

u32 FrameEvent::msec() const {
    return (u32)(nsec / 1000000ull);
}
