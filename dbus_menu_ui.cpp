#include "dbus_menu_ui.h"

#include "composer.h"
#include "dbus_menu.h"
#include "icon.h"
#include "imgui_wm.h"
#include "util.h"

using namespace stl;

namespace {
    Icon* itemIcon(Composer& c, DBusMenuItem& item, u32 desired) {
        if (item.iconData) {
            return item.iconData;
        }

        return item.iconName.empty() ? nullptr : c.findIcon(sv(item.iconName), desired);
    }

    void pushDisposition(DBusMenuDisposition disposition) {
        ImVec4 color;

        switch (disposition) {
            case DBusMenuDisposition::informative:
                color = ImVec4(0.45f, 0.72f, 1.f, 1.f);
                break;
            case DBusMenuDisposition::warning:
                color = ImVec4(1.f, 0.72f, 0.24f, 1.f);
                break;
            case DBusMenuDisposition::alert:
                color = ImVec4(1.f, 0.35f, 0.30f, 1.f);
                break;
            case DBusMenuDisposition::normal:
                color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
    }

    void drawIcon(Composer& c, DBusMenuItem& item) {
        float size = ImGui::GetFontSize();
        Icon* icon = itemIcon(c, item, (u32)size);
        u64 texture = c.iconResolver && icon ? c.iconResolver->iconTexture(icon) : 0;

        if (!texture) {
            return;
        }

        ImVec2 min = ImGui::GetItemRectMin();
        float y = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y - size) * 0.5f;

        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)texture,
            ImVec2(min.x + ImGui::GetStyle().FramePadding.x, y),
            ImVec2(min.x + ImGui::GetStyle().FramePadding.x + size, y + size));
    }

    const char* displayLabel(DBusMenuItem& item, bool withIcon) {
        if (item.label.empty()) {
            return "(unnamed)";
        }

        if (!withIcon) {
            return item.label.cStr();
        }

        auto& out = sb();

        out << "   "_sv << sv(item.label);

        return out.cStr();
    }

    void drawItems(Composer& c, DBusMenu& menu, Vector<DBusMenuItem*>& items) {
        for (DBusMenuItem* item : items) {
            if (!item->visible) {
                continue;
            }

            if (item->separator) {
                ImGui::Separator();

                continue;
            }

            ImGui::PushID(item->id);
            Icon* icon = itemIcon(c, *item, (u32)ImGui::GetFontSize());
            const char* label = displayLabel(*item, icon != nullptr);
            const char* shortcut = item->shortcut.empty() ? nullptr : item->shortcut.cStr();

            pushDisposition(item->disposition);

            if (item->submenu || !item->children.empty()) {
                bool open = ImGui::BeginMenu(label, item->enabled);

                if (open && !item->open) {
                    menu.prepare(item->id);
                }

                item->open = open;
                drawIcon(c, *item);

                if (open) {
                    if (item->children.empty()) {
                        ImGui::MenuItem("loading...", nullptr, false, false);
                    } else {
                        drawItems(c, menu, item->children);
                    }

                    ImGui::EndMenu();
                }
            } else {
                bool selected = item->toggle == DBusMenuToggle::checkmark && item->toggleState > 0;

                if (ImGui::MenuItem(label, shortcut, selected, item->enabled)) {
                    menu.activate(item->id);
                }

                drawIcon(c, *item);

                if (item->toggle == DBusMenuToggle::radio) {
                    ImVec2 max = ImGui::GetItemRectMax();
                    float radius = ImGui::GetFontSize() * 0.23f;
                    ImVec2 p(max.x - ImGui::GetStyle().FramePadding.x - radius * 2.f,
                             (ImGui::GetItemRectMin().y + max.y) * 0.5f);
                    ImDrawList* draw = ImGui::GetWindowDrawList();

                    draw->AddCircle(p, radius, ImGui::GetColorU32(ImGuiCol_Text), 0, 1.5f);

                    if (item->toggleState > 0) {
                        draw->AddCircleFilled(p, radius * 0.48f, ImGui::GetColorU32(ImGuiCol_Text));
                    }
                }
            }

            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
}

void drawDBusMenuItems(Composer& c, DBusMenu& menu, Vector<DBusMenuItem*>& items) {
    drawItems(c, menu, items);
}

void drawDBusMenuBar(Composer& c, DBusMenu& menu) {
    for (DBusMenuItem* item : menu.items) {
        if (!item->visible || item->separator) {
            continue;
        }

        ImGui::PushID(item->id);
        pushDisposition(item->disposition);

        const char* label = item->label.empty() ? "(unnamed)" : item->label.cStr();
        bool submenu = item->submenu || !item->children.empty();

        if (submenu) {
            bool clicked = ImGui::MenuItem(label, nullptr, item->open, item->enabled);

            if (clicked) {
                if (item->open) {
                    item->open = false;
                } else {
                    item->open = true;
                    menu.prepare(item->id);
                    ImGui::OpenPopup("##dbus-root");
                }
            }

            if (item->open && ImGui::BeginPopup("##dbus-root")) {
                if (item->children.empty()) {
                    ImGui::MenuItem("loading...", nullptr, false, false);
                } else {
                    drawItems(c, menu, item->children);
                }

                ImGui::EndPopup();
            } else if (item->open && !ImGui::IsPopupOpen("##dbus-root")) {
                item->open = false;
            }
        } else if (ImGui::MenuItem(label, nullptr, false, item->enabled)) {
            menu.activate(item->id);
        }

        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}
