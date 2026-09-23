#include "keyboard.h"

#include "log.h"
#include "util.h"
#include "log_extern.h"
#include "pooled.h"
#include "composer.h"
#include "listener.h"
#include "intr_list.h"
#include "chaos_monkey.h"

#include <std/ios/sys.h>
#include <std/dbg/verify.h>
#include <std/mem/obj_pool.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xkbcommon/xkbcommon.h>

using namespace stl;

namespace {
    struct KeyboardImpl: public Keyboard {
        Composer* c = nullptr;
        xkb_context* ctx = nullptr;
        xkb_keymap* keymap = nullptr;
        xkb_state* state = nullptr;
        int fd = -1;
        u32 size = 0;

        KeyboardImpl(ObjPool& pool, Composer& c, StringView layout, StringView options, u32 group);

        void updateKey(u32 evdevCode, bool pressed) override;
        void setGroup(u32 group) override;
        void layoutMaybeSwitched(u32 changed);
        KeyMods mods() const override;
        u32 modMask() const override;
        u32 keysymBase(u32 evdevCode) const override;
        size_t utf8(u32 evdevCode, char* buf, size_t cap) const override;
        int keymapFd() const override;
        u32 keymapSize() const override;
        void layoutShort(char out[4]) const override;
        u32 layoutCount() const override;
        StringView layoutName(u32 group) const override;
        u32 activeLayout() const override;
    };
}

namespace {
    void xkbLog(struct xkb_context* ctx, enum xkb_log_level, const char* fmt, va_list args) {
        externVLog(*((KeyboardImpl*)xkb_context_get_user_data(ctx))->c->log, "xkb"_sv, fmt, args);
    }
}

// every xkb object and the keymap file register their release in the
// keyboard's pool as they are made: a keyboard that cannot be finished
// throws and leaves them to that pool
KeyboardImpl::KeyboardImpl(ObjPool& pool, Composer& comp, StringView layout, StringView options, u32 group)
    : c(&comp)
{
    ctx = c->chaos->xkbContext(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
    STD_VERIFY(ctx);

    xkb_context* heldCtx = ctx;

    pooledGuard(pool, [heldCtx] {
        xkb_context_unref(heldCtx);
    });

    xkb_context_set_user_data(ctx, this);
    xkb_context_set_log_fn(ctx, xkbLog);

    Buffer lb(layout), ob(options);
    xkb_rule_names names{};

    names.layout = lb.cStr();
    names.options = ob.cStr();
    keymap = c->chaos->xkbKeymap(xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS));

    // with no layout and no options the first try already was the
    // defaults: the retry fails the same way
    if (!keymap) {
        *c->log << "imway: bad xkb layout/options, falling back to defaults"_sv << endL;
        keymap = c->chaos->xkbKeymap(xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS));
    }

    if (keymap) {
        xkb_keymap* heldKeymap = keymap;

        pooledGuard(pool, [heldKeymap] {
            xkb_keymap_unref(heldKeymap);
        });

        state = c->chaos->xkbState(xkb_state_new(keymap));
    }

    if (state) {
        xkb_state* heldState = state;

        pooledGuard(pool, [heldState] {
            xkb_state_unref(heldState);
        });

        char* text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);

        size = (u32)StringView(text).length() + 1;
        // the same fd is duped to every client, and wl_seat v5+ lets them
        // map it MAP_SHARED: sealed below, so no client can truncate or
        // rewrite the keymap
        fd = c->chaos->keymapFile(memfd_create("imway-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING));

        if (fd >= 0) {
            int heldFd = fd;

            pooledGuard(pool, [heldFd] {
                close(heldFd);
            });
        }

        bool written = fd >= 0 && c->chaos->keymapWrite(write(fd, text, size)) == (ssize_t)size;

        free(text);

        if (!written) {
            fd = -1;
        }
    }

    if (fd < 0) {
        StringView failed = !keymap ? "no keymap compiles"_sv : !state ? "no xkb state for it"_sv : "its file cannot be written"_sv;

        *c->log << "imway: keymap unusable: "_sv << failed << endL;
    }

    STD_VERIFY(fd >= 0);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);

    u32 count = xkb_keymap_num_layouts(keymap);

    if (count && group >= count) {
        group = count - 1;
    }

    xkb_state_update_mask(state, 0, 0, 0, 0, 0, group);
}

void KeyboardImpl::updateKey(u32 evdevCode, bool pressed) {
    layoutMaybeSwitched(xkb_state_update_key(state, evdevCode + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP));
}

void KeyboardImpl::setGroup(u32 group) {
    u32 dep = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    u32 lat = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    u32 lock = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);

    layoutMaybeSwitched(xkb_state_update_mask(state, dep, lat, lock, 0, 0, group));
}

// a layout switch hotkey and an explicit switch announce themselves alike
void KeyboardImpl::layoutMaybeSwitched(u32 changed) {
    if (changed & XKB_STATE_LAYOUT_EFFECTIVE) {
        forEach<Listener>(c->layoutSwitchedListeners, [](Listener& listener) {
            listener.onListen();
        });
    }
}

KeyMods KeyboardImpl::mods() const {
    KeyMods m;

    m.depressed = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    m.latched = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    m.locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    m.group = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

    return m;
}

u32 KeyboardImpl::modMask() const {
    u32 mask = 0;

    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= kModShift;
    }

    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= kModCtrl;
    }

    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= kModAlt;
    }

    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= kModLogo;
    }

    return mask;
}

u32 KeyboardImpl::keysymBase(u32 evdevCode) const {
    const xkb_keysym_t* syms = nullptr;
    int n = xkb_keymap_key_get_syms_by_level(keymap, evdevCode + 8, 0, 0, &syms);

    return n > 0 ? syms[0] : XKB_KEY_NoSymbol;
}

size_t KeyboardImpl::utf8(u32 evdevCode, char* buf, size_t cap) const {
    int n = xkb_state_key_get_utf8(state, evdevCode + 8, buf, cap);

    return n > 0 ? (size_t)n : 0;
}

int KeyboardImpl::keymapFd() const {
    return fd;
}

u32 KeyboardImpl::keymapSize() const {
    return size;
}

void KeyboardImpl::layoutShort(char out[4]) const {
    u32 group = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
    const char* name = xkb_keymap_layout_get_name(keymap, group);
    StringView n(name ? name : "??");

    for (u32 i = 0; i < 2; i++) {
        u8 c = i < n.length() ? n[i] : '?';

        if (c >= 'a' && c <= 'z') {
            c = (u8)(c - 'a' + 'A');
        }

        out[i] = (char)c;
    }

    out[2] = 0;
}

u32 KeyboardImpl::layoutCount() const {
    return xkb_keymap_num_layouts(keymap);
}

StringView KeyboardImpl::layoutName(u32 group) const {
    const char* name = xkb_keymap_layout_get_name(keymap, group);

    return name ? StringView(name) : "?"_sv;
}

u32 KeyboardImpl::activeLayout() const {
    return xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
}

Keyboard* Keyboard::create(ObjPool* pool, Composer& c, StringView layout, StringView options, u32 group) {
    return pool->make<KeyboardImpl>(*pool, c, layout, options, group);
}
