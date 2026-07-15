#include "xdg_utils.h"
#include "util.h"

#include <stdlib.h>

using namespace stl;

void forEachXdgDataDir(DirVisitorFace& v) {
    if (const char* home = getenv("XDG_DATA_HOME")) {
        v.visit(StringView(home));
    } else if (const char* h = getenv("HOME")) {
        // the visitor formats paths of its own: this one keeps its own
        StringBuilder p;

        p << h << "/.local/share"_sv;
        v.visit(sv(p));
    }

    const char* xdg = getenv("XDG_DATA_DIRS");
    StringView rest(xdg ? xdg : "/usr/local/share:/usr/share");

    while (!rest.empty()) {
        StringView one, tail;

        if (!rest.split(':', one, tail)) {
            one = rest;
            tail = {};
        }

        if (!one.empty()) {
            v.visit(one);
        }

        rest = tail;
    }
}
