#include "keyboard.h"

#include "log.h"
#include "util.h"
#include "log_extern.h"
#include "pooled.h"
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
        Log* log = nullptr;
        ChaosMonkey* chaos = nullptr;
        xkb_context* ctx = nullptr;
        // the keymap generation: the keymap, its state and its file are made
        // together into one arena and dropped together when the next replaces
        // them; the keyboard's pool deletes the last one
        ObjPool** gen = nullptr;
        xkb_keymap* keymap = nullptr;
        xkb_state* state = nullptr;
        int fd = -1;
        u32 size = 0;

        KeyboardImpl(ObjPool& pool, Log& log, ChaosMonkey& chaos, StringView layout, StringView options);

        void configure(StringView layout, StringView options) override;
        void updateKey(u32 evdevCode, bool pressed) override;
        void setGroup(u32 group) override;
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
        externVLog(*((KeyboardImpl*)xkb_context_get_user_data(ctx))->log, "xkb"_sv, fmt, args);
    }
}

KeyboardImpl::KeyboardImpl(ObjPool& pool, Log& l, ChaosMonkey& c, StringView layout, StringView options)
    : log(&l)
    , chaos(&c)
{
    ctx = chaos->xkbContext(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
    STD_VERIFY(ctx);

    xkb_context* heldCtx = ctx;

    pooledGuard(pool, [heldCtx] {
        xkb_context_unref(heldCtx);
    });

    xkb_context_set_user_data(ctx, this);
    xkb_context_set_log_fn(ctx, xkbLog);

    gen = pool.make<ObjPool*>(nullptr);

    ObjPool** heldGen = gen;

    pooledGuard(pool, [heldGen] {
        delete *heldGen;
    });

    configure(layout, options);
}

// the first call, at boot, has no keymap to keep: a failure there throws,
// and what the keyboard made so far goes with its pool
void KeyboardImpl::configure(StringView layout, StringView options) {
    Buffer lb(layout), ob(options);
    xkb_rule_names names{};

    names.layout = lb.cStr();
    names.options = ob.cStr();

    // the next generation is made in an arena of its own: kept whole if
    // every part of it came out, dropped whole if any did not
    ObjPool* next = ObjPool::fromMemoryRaw();
    xkb_keymap* nextKeymap = chaos->xkbKeymap(xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS));

    // with no layout and no options the first try already was the
    // defaults: the retry fails the same way
    if (!nextKeymap) {
        *log << "imway: bad xkb layout/options, falling back to defaults"_sv << endL;
        nextKeymap = chaos->xkbKeymap(xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS));
    }

    if (nextKeymap) {
        pooledGuard(*next, [nextKeymap] {
            xkb_keymap_unref(nextKeymap);
        });
    }

    xkb_state* nextState = nextKeymap ? chaos->xkbState(xkb_state_new(nextKeymap)) : nullptr;

    if (nextState) {
        pooledGuard(*next, [nextState] {
            xkb_state_unref(nextState);
        });
    }

    int nextFd = -1;
    u32 nextSize = 0;
    bool written = false;

    if (nextState) {
        char* text = xkb_keymap_get_as_string(nextKeymap, XKB_KEYMAP_FORMAT_TEXT_V1);

        nextSize = (u32)StringView(text).length() + 1;
        // the same fd is duped to every client, and wl_seat v5+ lets them
        // map it MAP_SHARED: sealed below, so no client can truncate or
        // rewrite the keymap
        nextFd = chaos->keymapFile(memfd_create("imway-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING));

        if (nextFd >= 0) {
            pooledGuard(*next, [nextFd] {
                close(nextFd);
            });

            written = chaos->keymapWrite(write(nextFd, text, nextSize)) == (ssize_t)nextSize;
        }

        free(text);
    }

    if (!written) {
        StringView failed = !nextKeymap ? "no keymap compiles"_sv : !nextState ? "no xkb state for it"_sv : "its file cannot be written"_sv;

        delete next;
        *log << "imway: keymap unusable: "_sv << failed << endL;
        STD_VERIFY(keymap);
        *log << "imway: keeping the current keymap"_sv << endL;

        return;
    }

    fcntl(nextFd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);

    // the active group carries over as far as the new list reaches
    u32 group = state ? xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) : 0;
    u32 count = xkb_keymap_num_layouts(nextKeymap);

    if (count && group >= count) {
        group = count - 1;
    }

    xkb_state_update_mask(nextState, 0, 0, 0, 0, 0, group);

    delete *gen;
    *gen = next;
    fd = nextFd;
    size = nextSize;
    state = nextState;
    keymap = nextKeymap;
}

void KeyboardImpl::updateKey(u32 evdevCode, bool pressed) {
    xkb_state_update_key(state, evdevCode + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
}

void KeyboardImpl::setGroup(u32 group) {
    u32 dep = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    u32 lat = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    u32 lock = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);

    xkb_state_update_mask(state, dep, lat, lock, 0, 0, group);
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

Keyboard* Keyboard::create(ObjPool* pool, Log& log, ChaosMonkey& chaos, StringView layout, StringView options) {
    return pool->make<KeyboardImpl>(*pool, log, chaos, layout, options);
}
