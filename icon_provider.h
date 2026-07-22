#pragma once

#include <std/lib/node.h>
#include <std/str/view.h>
#include <std/sys/types.h>

struct Icon;

// One source of icons in the Composer registry. The icon stays owned by the
// provider: use the result before returning to the event loop, or copy the
// pixels out — nobody stores an Icon* across frames. Lookup is by the 64-bit
// symbol of the id; ids never collide across providers (icon names, app ids,
// paths and the synthetic per-window keys are disjoint by construction), and
// equal ids within one namespace are arbitrated by registry order. The id
// string itself is only load material for a cold cache miss, and is empty
// when the caller resolved a precomputed symbol.
// the node links it into Composer::iconProviders
struct IconProvider: stl::IntrusiveNode {
    virtual Icon* findIcon(u64 sym, stl::StringView id) = 0;

    ~IconProvider() noexcept;
};
