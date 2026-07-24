#include "icon_provider.h"

#include "scene.h"
#include "composer.h"

IconProvider::~IconProvider() noexcept {
    unlink();
}

// lives here rather than in scene.cpp so the scene stays linkable without
// the Composer registry (pixel tests link scene.cpp standalone)
Icon* Toplevel::icon(Composer& c) const {
    if (Icon* found = c.findIcon(iconSym)) {
        return found;
    }

    if (iconNameSym) {
        if (Icon* found = c.findIcon(iconNameSym)) {
            return found;
        }
    }

    return c.findIcon(appIdSym);
}
