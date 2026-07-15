#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

// modifier mask for key bindings, resolved from the real xkb state
inline constexpr u32 kModShift = 1u << 0;
inline constexpr u32 kModCtrl = 1u << 1;
inline constexpr u32 kModAlt = 1u << 2;
inline constexpr u32 kModLogo = 1u << 3;

struct KeyMods {
    u32 depressed = 0, latched = 0, locked = 0, group = 0;
};

// the single owner of xkb state: clients and the compositor ui both live off
// this one keymap and group, so they can never disagree about the layout
struct Keyboard {
    virtual void updateKey(u32 evdevCode, bool pressed) = 0;

    // switch the active layout group, preserving modifier state
    virtual void setGroup(u32 group) = 0;

    virtual KeyMods mods() const = 0;
    virtual u32 modMask() const = 0;

    // keysym in group 0 / level 0, for layout-independent bindings
    virtual u32 keysymBase(u32 evdevCode) const = 0;

    // utf8 of the key under the CURRENT group and modifiers, NUL-terminated
    virtual size_t utf8(u32 evdevCode, char* buf, size_t cap) const = 0;

    virtual int keymapFd() const = 0;
    virtual u32 keymapSize() const = 0;

    // two-letter uppercase name of the active layout, e.g. EN / RU
    virtual void layoutShort(char out[4]) const = 0;

    static Keyboard* create(stl::ObjPool* pool, const char* layout, const char* options);
};
