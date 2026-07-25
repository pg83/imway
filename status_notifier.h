#pragma once

#include "visitor.h"

#include <std/sys/types.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>

struct Composer;
struct DBusMenu;
struct Icon;
struct StatusNotifierItem;

enum class StatusActionKind {
    primary,
    context,
};

// An opaque command embedded in the UI model.  The dock renders the model;
// StatusNotifier is only asked to execute the selected command.
struct StatusAction {
    StatusNotifierItem* item = nullptr;
    StatusActionKind kind = StatusActionKind::primary;
};

// Read-only from the dock's point of view.  Named icons resolve through the
// provider registry; pixmaps stay owned by StatusNotifier, served under the
// precomputed symbols below (0 = the item never had a pixmap key).
struct StatusNotifierItem {
    stl::StringBuilder id;
    stl::StringBuilder title;
    stl::StringBuilder desktopEntry;
    stl::StringBuilder status;
    stl::StringBuilder iconName;
    stl::StringBuilder attentionIconName;

    u64 iconSym = 0;
    u64 attentionIconSym = 0;

    StatusAction primary;
    StatusAction context;
    DBusMenu* menu = nullptr;

    bool hasMenu = false;
    bool itemIsMenu = false;
};

struct StatusNotifier {
    virtual void itemsImpl(stl::VisitorFace&& vis) = 0;

    template <typename F>
    void items(F f) {
        itemsImpl(visitEach<StatusNotifierItem>(f));
    }

    virtual void activate(const StatusAction& action, int x, int y) = 0;

    static StatusNotifier* create(Composer& c);
};
