#include "keyboard.h"
#include "log.h"
#include "log_extern.h"
#include "util.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <xkbcommon/xkbcommon.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct KeyboardImpl: public Keyboard {
        Log* log = nullptr;
        xkb_context* ctx = nullptr;
        xkb_keymap* keymap = nullptr;
        xkb_state* state = nullptr;
        int fd = -1;
        u32 size = 0;

        KeyboardImpl(Log& log, StringView layout, StringView options);
        ~KeyboardImpl() noexcept;

        void updateKey(u32 evdevCode, bool pressed) override;
        void setGroup(u32 group) override;
        KeyMods mods() const override;
        u32 modMask() const override;
        u32 keysymBase(u32 evdevCode) const override;
        size_t utf8(u32 evdevCode, char* buf, size_t cap) const override;
        int keymapFd() const override;
        u32 keymapSize() const override;
        void layoutShort(char out[4]) const override;
    };
}

namespace {
    void xkbLog(struct xkb_context* ctx, enum xkb_log_level, const char* fmt, va_list args) {
        externVLog(*((KeyboardImpl*)xkb_context_get_user_data(ctx))->log, "xkb"_sv, fmt, args);
    }
}

KeyboardImpl::KeyboardImpl(Log& l, StringView layout, StringView options)
    : log(&l)
{
    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    STD_VERIFY(ctx);
    xkb_context_set_user_data(ctx, this);
    xkb_context_set_log_fn(ctx, xkbLog);

    // xkb wants NUL-terminated strings: materialize right at the call
    Buffer lb(layout), o(options);
    xkb_rule_names names{};

    names.layout = lb.cStr();
    names.options = o.cStr();
    keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!keymap && (!layout.empty() || !options.empty())) {
        *log << "imway: bad xkb layout/options, falling back to defaults"_sv << endL;
        keymap = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    STD_VERIFY(keymap);

    state = xkb_state_new(keymap);
    STD_VERIFY(state);

    char* str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);

    size = (u32)StringView(str).length() + 1;
    // the same fd is duped to every client, and wl_seat v5+ lets them map it
    // MAP_SHARED — seal it so no client can truncate or rewrite the keymap
    fd = memfd_create("imway-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);

    bool written = fd >= 0 && write(fd, str, size) == (ssize_t)size;

    free(str);
    STD_VERIFY(written);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
}

KeyboardImpl::~KeyboardImpl() noexcept {
    if (fd >= 0) {
        close(fd);
    }

    if (state) {
        xkb_state_unref(state);
    }

    if (keymap) {
        xkb_keymap_unref(keymap);
    }

    if (ctx) {
        xkb_context_unref(ctx);
    }
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

Keyboard* Keyboard::create(ObjPool* pool, Log& log, StringView layout, StringView options) {
    return pool->make<KeyboardImpl>(log, layout, options);
}
