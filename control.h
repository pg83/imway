#pragma once

#include <std/str/view.h>

struct Composer;

struct Control {
    static Control* create(Composer& c, stl::StringView fifoPath);
};
