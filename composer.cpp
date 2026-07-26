#include "composer.h"

#include "icon.h"
#include "intr_list.h"
#include "input_router.h"
#include "icon_provider.h"

using namespace stl;

namespace {
    // the smallest icon that still covers the target beats everything (the
    // least downscale); when nobody covers it, the largest one loses the
    // least when upscaled. Ties keep the earlier provider's answer.
    bool betterFit(const Icon& cand, const Icon& inc, u32 desired) {
        u32 c = (u32)(cand.width > cand.height ? cand.width : cand.height);
        u32 i = (u32)(inc.width > inc.height ? inc.width : inc.height);

        if ((c >= desired) != (i >= desired)) {
            return c >= desired;
        }

        return c >= desired ? c < i : c > i;
    }
}

Composer::Composer(ObjPool* p)
    : pool(p)
{
    initializeSettings(settings);
    entry = createInputRouter(*this);
}

Icon* Composer::findIcon(StringView id, u32 desired) {
    return findIcon(id.hash64(), desired, id);
}

Icon* Composer::findIcon(u64 sym, u32 desired, StringView id) {
    Icon* best = nullptr;

    for (IconProvider* provider : each<IconProvider>(iconProviders)) {
        Icon* icon = provider->findIcon(sym, desired, id);

        if (icon && (!best || betterFit(*icon, *best, desired))) {
            best = icon;
        }
    }

    return best;
}
