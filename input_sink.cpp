#include "input_sink.h"

InputSink::~InputSink() noexcept {
    unlink();
}
