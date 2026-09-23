#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Composer;

// modifier mask for key bindings, resolved from the real xkb state
inline constexpr u32 kModShift = 1u << 0;
inline constexpr u32 kModCtrl = 1u << 1;
inline constexpr u32 kModAlt = 1u << 2;
inline constexpr u32 kModLogo = 1u << 3;

struct KeyMods {
    u32 depressed = 0, latched = 0, locked = 0, group = 0;
};

// the single owner of xkb state: clients and the compositor ui both live off
// this one keymap and group, so they can never disagree about the layout.
// A keyboard is built once for its layouts and options; other layouts are
// another keyboard, made in a pool of its own that replaces the old one in
// the Composer. Nobody keeps a Keyboard*: it is always read from there.
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

    // configured layout groups, for the settings input page
    virtual u32 layoutCount() const = 0;
    virtual stl::StringView layoutName(u32 group) const = 0;
    virtual u32 activeLayout() const = 0;

    // a keymap that cannot be built, its state or its sealed file throws,
    // the reason logged; group is the layout to start in, clamped to the new
    // list
    static Keyboard* create(stl::ObjPool* pool, Composer& c, stl::StringView layout, stl::StringView options, u32 group);
};
