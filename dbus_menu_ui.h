#pragma once

#include <std/lib/vector.h>

struct Composer;
struct DBusMenu;
struct DBusMenuItem;

// Popup renderer shared by the dock and nested global-menu entries.
void drawDBusMenuItems(Composer& c, DBusMenu& menu, stl::Vector<DBusMenuItem*>& items);

// Renders the root children as native entries of the current ImGui menu bar.
void drawDBusMenuBar(Composer& c, DBusMenu& menu);
