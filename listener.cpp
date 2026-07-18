#include "listener.h"

Listener::~Listener() noexcept {
    unlink();
}
