#include "composer.h"

#include "icon.h"
#include "keyboard.h"
#include "listener.h"
#include "intr_list.h"
#include "input_router.h"
#include "icon_provider.h"

#include <std/mem/obj_pool.h>

using namespace stl;

Composer::Composer(ObjPool* p)
    : pool(p)
{
    settings = Settings::create(*this);
    applySettingsEnvironment(*settings);
    entry = createInputRouter(*this);
}

Icon* Composer::findIcon(StringView id, u32 desired) {
    return findIcon(id.hash64(), desired, id);
}

// the providers answer disjoint keys: the icon store hashes of desktop file
// ids, icon file names and absolute paths, the tray hashes of its items'
// service and path (a path has a '/', which none of the store's names can
// contain unless it is an absolute path, and an item's key never starts
// with one), the toplevels hashes of the four bytes of their numeric ids
// (below 2^24 the last byte is a NUL, which no name holds). The first
// answer is the only one
Icon* Composer::findIcon(u64 sym, u32 desired, StringView id) {
    for (IconProvider* provider : each<IconProvider>(iconProviders)) {
        if (Icon* icon = provider->findIcon(sym, desired, id)) {
            return icon;
        }
    }

    return nullptr;
}

void Composer::rebuildKeyboard(StringView layouts, StringView options) {
    ObjPool* next = ObjPool::fromMemoryRaw();
    Keyboard* built = nullptr;

    try {
        built = Keyboard::create(next, *this, layouts, options, kb ? kb->activeLayout() : 0);
    } catch (...) {
        delete next;

        throw;
    }

    delete kbPool;
    kbPool = next;
    kb = built;

    forEach<Listener>(keyboardListeners, [](Listener& listener) {
        listener.onListen();
    });
}
