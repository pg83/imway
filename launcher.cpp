#include "launcher.h"
#include "composer.h"
#include "dialog.h"
#include "icon.h"
#include "icon_store.h"
#include "scene.h"
#include "util.h"
#include "xdg_utils.h"

#include <imgui.h>

#include <std/alg/qsort.h>
#include <std/ios/fs_utils.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/sys/fs.h>

using namespace stl;

namespace {
    // one launcher row — a .desktop entry or a compositor action; the strings
    // live in the dialog blob as offsets, so rows stay trivial for stl::Vector
    struct Row {
        u32 name = 0, nameLen = 0;
        u32 exec = 0, execLen = 0;
        u32 icon = 0, iconLen = 0;
        LauncherAction action = LauncherAction::none;
        bool terminal = false;
    };

    // Dialog and all of its member storage are retired by one arena teardown.
    struct Dialog {
        bool fresh = true;
        bool focusField = true;
        // raw buffer by imgui InputText contract
        char query[256] = "";
        long sel = 0;

        StringBuilder blob;
        Vector<Row> rows;
        Vector<u32> vis;

        // case-folding scratch for refilter; a .desktop Name can be longer
        // than any fixed buffer, so grow instead of overflowing the stack
        Buffer queryLower;
        Buffer nameLower;

        Dialog();

        StringView view(u32 off, u32 len) const;

        void rescan();
        void addAction(StringView name, StringView icon, LauncherAction action);
        void parseDesktop(StringBuilder& file);
        void refilter();

        // pure drawing: state transitions stay in drawLauncher, the only
        // outward signs are run/action/picked and the open flag dropping
        bool draw(Composer& c, bool& open, Buffer& run, LauncherAction& action,
                  bool& terminal, float anchorX, float anchorY);
    };
}

// strip the %f/%u/... field codes from Exec
static void appendExec(StringBuilder& out, StringView val) {
    const u8* b = val.begin();
    const u8* seg = b;

    while (b < val.end()) {
        if (*b == '%' && b + 1 < val.end()) {
            out << StringView(seg, b);
            b += 2;
            seg = b;
        } else {
            b++;
        }
    }

    out << StringView(seg, b);
}

Dialog::Dialog() {
    rescan();
}

StringView Dialog::view(u32 off, u32 len) const {
    return StringView((const u8*)blob.data() + off, (size_t)len);
}

// a compositor action, mixed in and sorted among the programs
void Dialog::addAction(StringView name, StringView icon, LauncherAction action) {
    Row r;

    r.name = (u32)blob.used();
    r.nameLen = (u32)name.length();
    blob << name;
    r.exec = (u32)blob.used();
    r.execLen = 0;
    r.icon = (u32)blob.used();
    r.iconLen = (u32)icon.length();
    blob << icon;
    r.action = action;
    rows.pushBack(r);
}

void Dialog::rescan() {
    addAction("lock screen"_sv, "system-lock-screen"_sv, LauncherAction::lockScreen);
    addAction("settings"_sv, "preferences-system"_sv, LauncherAction::settings);
    addAction("notifications"_sv, "preferences-system-notifications"_sv, LauncherAction::notifications);
    addAction("inspector"_sv, "utilities-system-monitor"_sv, LauncherAction::inspector);
    addAction("color picker"_sv, "color-select"_sv, LauncherAction::colorPicker);

    forEachXdgDataDir([this](StringView base) {
        StringBuilder dir;

        dir << base << "/applications"_sv;

        // missing xdg dirs are normal: listDir throws, skip them
        try {
            listDir(sv(dir), [this, &dir](const TPathInfo& e) {
                if (e.isDir || !e.item.endsWith(".desktop"_sv)) {
                    return;
                }

                StringBuilder f;

                f << sv(dir) << "/"_sv << e.item;
                parseDesktop(f);
            });
        } catch (...) {
        }
    });

    quickSort(rows.mutData(), rows.mutData() + rows.length(), [this](const Row& a, const Row& b) {
        return view(a.name, a.nameLen) < view(b.name, b.nameLen);
    });
}

void Dialog::parseDesktop(StringBuilder& file) {
    Buffer data;

    readFileContent(file, data);

    if (data.empty()) {
        return;
    }

    StringBuilder name, exec, icon;
    bool inSection = false;
    bool display = true;
    bool isApp = false;
    bool terminal = false;
    StringView rest = sv(data);

    while (!rest.empty()) {
        StringView line, tail;

        if (!rest.split('\n', line, tail)) {
            line = rest;
            tail = {};
        }

        rest = tail;
        line = line.stripCr();

        if (!line.empty() && line[0] == '[') {
            inSection = line == "[Desktop Entry]"_sv;

            continue;
        }

        if (!inSection) {
            continue;
        }

        StringView key, val;

        if (!line.split('=', key, val)) {
            continue;
        }

        if (key == "Name"_sv && name.empty()) {
            name << val;
        } else if (key == "Exec"_sv && exec.empty()) {
            appendExec(exec, val);
        } else if (key == "Icon"_sv && icon.empty()) {
            icon << val;
        } else if (key == "Type"_sv) {
            isApp = val == "Application"_sv;
        } else if (key == "Terminal"_sv) {
            terminal = val == "true"_sv;
        } else if ((key == "NoDisplay"_sv || key == "Hidden"_sv) && val == "true"_sv) {
            display = false;
        }
    }

    if (!isApp || !display || name.empty() || exec.empty()) {
        return;
    }

    Row r;

    r.name = (u32)blob.used();
    r.nameLen = (u32)name.used();
    blob << sv(name);
    r.exec = (u32)blob.used();
    r.execLen = (u32)exec.used();
    blob << sv(exec);
    r.icon = (u32)blob.used();
    r.iconLen = (u32)icon.used();
    blob << sv(icon);
    r.terminal = terminal;
    rows.pushBack(r);
}

void Dialog::refilter() {
    vis.clear();

    StringView ql = StringView(query).lower(queryLower);

    for (size_t i = 0; i < rows.length(); i++) {
        StringView nl = view(rows[i].name, rows[i].nameLen).lower(nameLower);

        if (ql.empty() || nl.search(ql)) {
            vis.pushBack((u32)i);
        }
    }
}

bool Dialog::draw(Composer& c, bool& open, Buffer& run, LauncherAction& action,
                  bool& terminal, float anchorX, float anchorY) {
    IconResolver& texes = *c.iconResolver;
    int screenW = c.scene->outW;
    int screenH = c.scene->outH;
    float uiScale = ImGui::GetStyle().FontScaleMain;

    refilter();

    bool picked = false;
    long n = (long)vis.length();

    // a row is either a compositor action or a command to spawn
    auto pick = [&](const Row& r) {
        if (r.action != LauncherAction::none) {
            action = r.action;
        } else {
            StringView ex = view(r.exec, r.execLen);

            run.append(ex.begin(), ex.length());
            terminal = r.terminal;
        }

        picked = true;
        open = false;
    };

    if (sel > n) {
        sel = n;
    }

    float lw = (float)screenW / 4.f < 320.f ? 320.f : (float)screenW / 4.f;

    if (anchorX >= 0.f) {
        ImGui::SetNextWindowPos(ImVec2(anchorX, anchorY), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(ImVec2((float)screenW / 2.f, (float)screenH / 4.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(lw, 0.f), ImVec2(lw, (float)screenH / 2.f));

    if (ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        if (fresh) {
            ImGui::SetWindowFocus();
            fresh = false;
        } else if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            open = false;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && sel < n) {
            sel++;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && sel > 0) {
            sel--;
        }

        ImGui::SetNextItemWidth(-1.f);

        if (focusField) {
            ImGui::SetKeyboardFocusHere();
            focusField = false;
        }

        bool enter = ImGui::InputText("##q", query, sizeof(query), ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::IsItemEdited()) {
            sel = 0;
        }

        float rowH = ImGui::GetFontSize() * 1.7f;
        bool navved = ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);

        for (long i = 0; i < n; i++) {
            const Row& r = rows[vis[(size_t)i]];
            bool selected = sel == i + 1;

            ImGui::PushID((int)vis[(size_t)i]);

            if (ImGui::Selectable("##row", selected, 0, ImVec2(0.f, rowH))) {
                pick(r);
            }

            if (selected && navved) {
                ImGui::SetScrollHereY(0.5f);
            }

            ImGui::SameLine(ImGui::GetStyle().FramePadding.x);

            if (u64 tex = texes.iconTexture(c.findIcon(view(r.icon, r.iconLen)))) {
                ImGui::Image((ImTextureID)tex, ImVec2(rowH, rowH));
            } else {
                ImGui::Dummy(ImVec2(rowH, rowH));
            }

            StringView name = view(r.name, r.nameLen);

            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFontSize()) / 2.f);
            ImGui::TextUnformatted((const char*)name.begin(), (const char*)name.end());
            ImGui::PopID();
        }

        if (enter && !picked) {
            if (sel >= 1 && sel <= n) {
                pick(rows[vis[(size_t)(sel - 1)]]);
            } else {
                // nothing highlighted: run the typed text as a command
                StringView cmd(query);

                run.append(cmd.begin(), cmd.length());
                picked = !run.empty();
                open = false;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            open = false;
        }
    }

    ImGui::End();

    return picked;
}

bool drawLauncher(Composer& c, bool toggle, DialogState** state, Buffer& run,
                  LauncherAction& action, bool& terminal, float anchorX, float anchorY) {
    action = LauncherAction::none;
    terminal = false;
    bool picked = false;

    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        picked = d.draw(c, open, run, action, terminal, anchorX, anchorY);
    });

    return picked;
}
