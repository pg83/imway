#include "input_sink.h"

InputSink::InputSink() noexcept {
    weak.anchor(this);
}

InputSink::~InputSink() noexcept {
    weak.invalidate();
    unlink();
}
