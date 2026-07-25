#pragma once

#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/types.h>

struct Composer;
struct Icon;

enum class DBusMenuToggle {
    none,
    checkmark,
    radio,
};

enum class DBusMenuDisposition {
    normal,
    informative,
    warning,
    alert,
};

struct DBusMenuItem {
    i32 id = 0;
    stl::StringBuilder label;
    stl::StringBuilder shortcut;
    stl::StringBuilder iconName;
    Icon* iconData = nullptr;
    stl::Vector<DBusMenuItem*> children;
    DBusMenuToggle toggle = DBusMenuToggle::none;
    DBusMenuDisposition disposition = DBusMenuDisposition::normal;
    i32 toggleState = -1;
    bool visible = true;
    bool enabled = true;
    bool separator = false;
    bool submenu = false;
    // renderer-local edge detector for AboutToShow; not remote model state
    bool open = false;
};

// One remote com.canonical.dbusmenu endpoint. The model is read-only to UI
// code and is replaced atomically after a complete GetLayout reply.
struct DBusMenu {
    stl::Vector<DBusMenuItem*> items;
    u32 revision = 0;
    bool ready = false;
    i32 activationRequested = 0;
    bool hasActivationRequest = false;

    virtual void prepare(i32 id) = 0;
    virtual void activate(i32 id) = 0;
};

// The one connection-level client/registrar. connect() creates an independent
// view of an endpoint; disconnect() is the matching lifetime operation.
struct DBusMenus {
    virtual DBusMenu* connect(stl::StringView service, stl::StringView path) = 0;
    virtual void disconnect(DBusMenu* menu) = 0;

    static DBusMenus* create(Composer& c);
};
