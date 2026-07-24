#include "composer.h"

#include "intr_list.h"
#include "input_router.h"
#include "icon_provider.h"

using namespace stl;

Composer::Composer(ObjPool* p)
    : pool(p)
{
    entry = createInputRouter(*this);
}

Icon* Composer::findIcon(StringView id) {
    return findIcon(id.hash64(), id);
}

Icon* Composer::findIcon(u64 sym, StringView id) {
    for (IconProvider* provider : each<IconProvider>(iconProviders)) {
        if (Icon* icon = provider->findIcon(sym, id)) {
            return icon;
        }
    }

    return nullptr;
}
