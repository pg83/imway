#include "dock.h"

#include "composer.h"
#include "icon.h"
#include "icon_store.h"
#include "intr_list.h"
#include "scene.h"
#include "status_notifier.h"
#include "util.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <std/lib/vector.h>
#include <std/str/view.h>

using namespace stl;

namespace {
    struct Group {
        StringView appId;
        Toplevel* active = nullptr;
        StatusNotifierItem* tray = nullptr;
        Icon* icon = nullptr;
        int windows = 0;
    };

    StringView normalizedAppId(StringView id) {
        return id.endsWith(".desktop"_sv) ? id.prefix(id.length() - 8) : id;
    }

    bool sameApp(StringView a, StringView b) {
        return !a.empty() && !b.empty() && normalizedAppId(a) == normalizedAppId(b);
    }

    void focus(Composer& c, Toplevel& t) {
        if (t.minimized) {
            t.minimized = false;
        }

        if (!t.activated) {
            t.raiseRequested = true;
        }

        c.scene->needsFrame = true;
    }

    void toggleMaximize(Composer& c, Toplevel& t) {
        if (!t.maximized) {
            t.restoreX = t.curX;
            t.restoreY = t.curY;
            t.restoreW = t.surface ? t.surface->geomW() : 0;
            t.restoreH = t.surface ? t.surface->geomH() : 0;
            t.restoreRequested = false;
            t.minimized = false;
            t.maximized = true;
        } else {
            t.maximized = false;
            t.restoreRequested = t.restoreW > 0 && t.restoreH > 0;
        }

        c.scene->needsFrame = true;
    }

    void drawLauncherGlyph(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color) {
        float cell = (max.x - min.x) / 8.f;
        float gap = cell * 0.9f;
        float total = cell * 3.f + gap * 2.f;
        float x0 = (min.x + max.x - total) * 0.5f;
        float y0 = (min.y + max.y - total) * 0.5f;

        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                ImVec2 a(x0 + x * (cell + gap), y0 + y * (cell + gap));

                draw->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), color, cell * 0.18f);
            }
        }
    }

    bool iconButton(const Theme& theme, const char* id, u64 texture, float size, bool active, bool attention = false) {
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(id, ImVec2(size, size));

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 max(p.x + size, p.y + size);

        if (ImGui::IsItemHovered()) {
            draw->AddRectFilled(p, max, themeColorU32(themeAlpha(theme.neutral[10], 0.08f)), 6.f);
        }

        if (texture) {
            float pad = size * 0.12f;

            draw->AddImage((ImTextureID)texture, ImVec2(p.x + pad, p.y + pad), ImVec2(max.x - pad, max.y - pad));
        } else {
            drawLauncherGlyph(draw, p, max, themeColorU32(theme.neutral[9]));
        }

        if (active) {
            draw->AddRectFilled(ImVec2(p.x, p.y + size * 0.25f), ImVec2(p.x + 3.f, p.y + size * 0.75f), themeColorU32(theme.accent), 1.5f);
        }

        if (attention) {
            draw->AddCircleFilled(ImVec2(max.x - 5.f, p.y + 5.f), 4.f, IM_COL32(255, 90, 70, 255));
        }

        return ImGui::IsItemClicked(ImGuiMouseButton_Left);
    }

    Icon* trayIcon(Composer& c, StatusNotifierItem& item, bool attention) {
        if (attention) {
            if (!item.attentionIconName.empty()) {
                if (Icon* icon = c.icons->byName(sv(item.attentionIconName))) {
                    return icon;
                }
            }

            if (item.attentionIconPixmap) {
                return item.attentionIconPixmap;
            }
        }

        if (!item.iconName.empty()) {
            if (Icon* icon = c.icons->byName(sv(item.iconName))) {
                return icon;
            }
        }

        return item.iconPixmap;
    }

    void drawTrayMenu(StatusNotifier& notifier, Vector<StatusMenuItem*>& entries, int x, int y) {
        for (StatusMenuItem* entry : entries) {
            if (!entry->visible) {
                continue;
            }

            if (entry->separator) {
                ImGui::Separator();

                continue;
            }

            const char* label = entry->label.empty() ? "(unnamed)" : entry->label.cStr();

            if (!entry->children.empty()) {
                bool open = ImGui::BeginMenu(label, entry->enabled);

                if (ImGui::IsItemActivated()) {
                    notifier.activate(entry->open, x, y);
                }

                if (open) {
                    drawTrayMenu(notifier, entry->children, x, y);
                    ImGui::EndMenu();
                }
            } else if (ImGui::MenuItem(label, nullptr, entry->checkable && entry->checked, entry->enabled)) {
                notifier.activate(entry->action, x, y);
            }
        }
    }
}

float dockBarWidth() {
    return 58.f * ImGui::GetStyle().FontScaleMain;
}

void drawDock(Composer& c, DockResult& result) {
    Scene& scene = *c.scene;
    float scale = ImGui::GetStyle().FontScaleMain;
    float width = dockBarWidth();
    float iconSize = 48.f * scale;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((width - iconSize) * 0.5f, 5.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 5.f * scale));
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowShadowCallback shadow = io.WindowShadowCallback;

    io.WindowShadowCallback = nullptr;
    bool open = ImGui::BeginViewportSideBar("##dock", ImGui::GetMainViewport(), ImGuiDir_Left, width, flags);
    io.WindowShadowCallback = shadow;

    if (open) {
        ImGui::PushID("launcher");

        if (iconButton(c.theme, "##icon", 0, iconSize, false)) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            result.launcher = true;
            result.launcherX = max.x + 8.f * scale;
            result.launcherY = min.y;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("launcher");
        }

        ImGui::PopID();
        ImGui::Separator();

        Vector<Group> groups;

        forEach<Toplevel>(scene.toplevels, [&](Toplevel& t) {
            if (!t.mapped || !t.surface) {
                return;
            }

            Group* group = nullptr;

            if (!t.appId.empty()) {
                for (Group* it = groups.mutBegin(); it != groups.mutEnd(); it++) {
                    Group& candidate = *it;

                    if (candidate.appId == sv(t.appId)) {
                        group = &candidate;

                        break;
                    }
                }
            }

            if (!group) {
                groups.pushBack({sv(t.appId), &t, nullptr, t.icon, 0});
                group = &groups.mutBack();
            }

            group->windows++;

            if (t.activated) {
                group->active = &t;
            } else if (!t.minimized && group->active->minimized) {
                group->active = &t;
            }

            if (!group->icon && t.icon) {
                group->icon = t.icon;
            }
        });

        if (c.statusNotifier) {
            c.statusNotifier->items([&](StatusNotifierItem& item) {
                StringView app = !item.desktopEntry.empty() ? sv(item.desktopEntry) : sv(item.id);
                Group* group = nullptr;

                for (Group* it = groups.mutBegin(); it != groups.mutEnd(); it++) {
                    if (!it->tray && sameApp(it->appId, app)) {
                        group = it;

                        break;
                    }
                }

                if (!group) {
                    // Passive items do not create a tray-only slot, but may
                    // still augment the icon/menu of a running application.
                    if (sv(item.status) == "Passive"_sv) {
                        return;
                    }

                    groups.pushBack({app, nullptr, &item, nullptr, 0});
                    group = &groups.mutBack();
                }

                group->tray = &item;
            });
        }

        for (Group* it = groups.mutBegin(); it != groups.mutEnd(); it++) {
            Group& group = *it;
            Toplevel* t = group.active;
            StatusNotifierItem* tray = group.tray;
            bool attention = tray && sv(tray->status) == "NeedsAttention"_sv;
            Icon* icon = tray ? trayIcon(c, *tray, attention) : nullptr;

            if (!icon) {
                icon = group.icon;
            }

            ImGui::PushID(t ? (void*)t : (void*)tray);

            u64 texture = c.iconResolver ? c.iconResolver->iconTexture(icon) : 0;
            bool clicked = iconButton(c.theme, "##icon", texture, iconSize, t && t->activated && !t->minimized, attention);

            if (clicked) {
                if (t) {
                    focus(c, *t);
                } else if (tray) {
                    ImVec2 mouse = ImGui::GetMousePos();

                    c.statusNotifier->activate(tray->primary, (int)mouse.x, (int)mouse.y);
                }
            }

            if (ImGui::IsItemHovered()) {
                StringView label;

                if (t && !t->title.empty()) {
                    label = sv(t->title);
                } else if (tray && !tray->title.empty()) {
                    label = sv(tray->title);
                } else {
                    label = group.appId;
                }

                if (label.empty()) {
                    ImGui::SetTooltip("application");
                } else {
                    ImGui::SetTooltip("%.*s", (int)label.length(), (const char*)label.begin());
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImVec2 mouse = ImGui::GetMousePos();

                if (tray && (tray->hasMenu || !t)) {
                    c.statusNotifier->activate(tray->context, (int)mouse.x, (int)mouse.y);
                }

                // A tray item without DBusMenu owns its ContextMenu itself;
                // otherwise the dock owns and draws the unified popup.
                if (t || (tray && tray->hasMenu)) {
                    ImGui::OpenPopup("##menu");
                }
            }

            if (ImGui::BeginPopup("##menu")) {
                if (t) {
                    if (ImGui::MenuItem("show", nullptr, false, t->minimized || !t->activated)) {
                        focus(c, *t);
                    }

                    if (ImGui::MenuItem("minimize", nullptr, false, !t->minimized)) {
                        t->minimized = true;

                        if (scene.focusedToplevel.get() == t) {
                            scene.focusedToplevel.reset();
                        }

                        scene.needsFrame = true;
                    }

                    if (ImGui::MenuItem(t->maximized ? "restore" : "maximize")) {
                        toggleMaximize(c, *t);
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("close")) {
                        t->closeRequested = true;
                        scene.needsFrame = true;
                    }
                }

                if (tray && tray->hasMenu) {
                    if (t) {
                        ImGui::Separator();
                    }

                    if (tray->menu.empty()) {
                        ImGui::MenuItem("loading...", nullptr, false, false);
                    } else {
                        ImVec2 mouse = ImGui::GetMousePos();

                        drawTrayMenu(*c.statusNotifier, tray->menu, (int)mouse.x, (int)mouse.y);
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);

}
