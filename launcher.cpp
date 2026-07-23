#include "launcher.h"
#include "composer.h"
#include "dialog.h"
#include "icon.h"
#include "icon_store.h"
#include "scene.h"
#include "util.h"
#include "xdg_utils.h"

#include <imgui.h>
#include <math.h>

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
        // display order: applications first, then the system actions; the
        // split point keeps the two grid groups addressable by one index
        Vector<u32> vis;
        long appsVis = 0;

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
    addAction("log"_sv, "utilities-terminal"_sv, LauncherAction::logView);

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
    appsVis = 0;

    StringView ql = StringView(query).lower(queryLower);

    // pass 0 collects applications (drawn on top), pass 1 the system
    // actions (the bottom group, next to the input line)
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < rows.length(); i++) {
            if ((rows[i].action != LauncherAction::none) != (pass == 1)) {
                continue;
            }

            StringView nl = view(rows[i].name, rows[i].nameLen).lower(nameLower);

            if (ql.empty() || nl.search(ql)) {
                vis.pushBack((u32)i);
                appsVis += pass == 0;
            }
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

    const ImGuiStyle& st = ImGui::GetStyle();
    float lw = fminf((float)screenW * 0.4f, 560.f * uiScale);
    float cell = 88.f * uiScale;
    float contentW = lw - st.WindowPadding.x * 2.f;
    long cols = (long)((contentW + st.ItemSpacing.x) / (cell + st.ItemSpacing.x));

    if (cols < 1) {
        cols = 1;
    }

    long appsN = appsVis;
    long sysN = n - appsN;

    if (anchorX >= 0.f) {
        // an anchor in the lower half (the dock's bottom launcher button)
        // grows the list upward, bottom-aligned to the anchor
        ImVec2 pivot(0.f, anchorY > (float)screenH * 0.5f ? 1.f : 0.f);

        ImGui::SetNextWindowPos(ImVec2(anchorX, anchorY), ImGuiCond_Always, pivot);
    } else {
        ImGui::SetNextWindowPos(ImVec2((float)screenW / 2.f, (float)screenH / 4.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(lw, 0.f), ImVec2(lw, (float)screenH));

    if (ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        if (fresh) {
            ImGui::SetWindowFocus();
            fresh = false;
        } else if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            open = false;
        }

        // spatial grid navigation over the two stacked groups; the input
        // line sits at the bottom, Up enters the grid from below, Down
        // walks back toward the input
        bool navved = false;

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && n) {
            navved = true;

            if (sel == 0) {
                long cnt = sysN ? sysN : appsN;
                long base = sysN ? appsN : 0;

                sel = base + ((cnt - 1) / cols) * cols + 1;
            } else {
                long i = sel - 1;
                bool inSys = i >= appsN;
                long gi = inSys ? i - appsN : i;
                long r = gi / cols, col = gi % cols;

                if (r > 0) {
                    long cnt = inSys ? sysN : appsN;
                    long target = (r - 1) * cols + col;

                    sel = (inSys ? appsN : 0) + (target < cnt ? target : cnt - 1) + 1;
                } else if (inSys && appsN) {
                    long target = ((appsN - 1) / cols) * cols + col;

                    sel = (target < appsN ? target : appsN - 1) + 1;
                }
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && sel > 0) {
            navved = true;

            long i = sel - 1;
            bool inSys = i >= appsN;
            long gi = inSys ? i - appsN : i;
            long r = gi / cols, col = gi % cols;
            long cnt = inSys ? sysN : appsN;

            if (r < (cnt - 1) / cols) {
                long target = (r + 1) * cols + col;

                sel = (inSys ? appsN : 0) + (target < cnt ? target : cnt - 1) + 1;
            } else if (!inSys && sysN) {
                sel = appsN + (col < sysN ? col : sysN - 1) + 1;
            } else {
                sel = 0;
            }
        }

        // one grid cell: a big icon, tooltip text on hover, accent frame on
        // the keyboard selection; missing icons render an initial plate
        auto cellItem = [&](long flat) {
            const Row& r = rows[vis[(size_t)flat]];
            bool selected = sel == flat + 1;

            ImGui::PushID((int)vis[(size_t)flat]);

            ImVec2 p = ImGui::GetCursorScreenPos();

            if (ImGui::InvisibleButton("##cell", ImVec2(cell, cell))) {
                pick(r);
            }

            if (selected && navved) {
                ImGui::SetScrollHereY(0.5f);
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pmax(p.x + cell, p.y + cell);
            StringView name = view(r.name, r.nameLen);

            if (ImGui::IsItemHovered()) {
                dl->AddRectFilled(p, pmax, themeColorU32(themeAlpha(c.theme.neutral[10], 0.08f)), 8.f);
                ImGui::SetTooltip("%.*s", (int)name.length(), (const char*)name.begin());
            }

            if (selected) {
                dl->AddRect(p, pmax, themeColorU32(c.theme.accent), 8.f, 0, 2.f);
            }

            float pad = cell * 0.15f;

            if (u64 tex = texes.iconTexture(c.findIcon(view(r.icon, r.iconLen)))) {
                dl->AddImage((ImTextureID)tex, ImVec2(p.x + pad, p.y + pad), ImVec2(pmax.x - pad, pmax.y - pad));
            } else {
                ImVec2 a(p.x + pad, p.y + pad), b(pmax.x - pad, pmax.y - pad);

                dl->AddRectFilled(a, b, themeColorU32(themeAlpha(c.theme.neutral[10], 0.12f)), 10.f);

                char ch = name.empty() ? '?' : (char)name[0];

                if (ch >= 'a' && ch <= 'z') {
                    ch = (char)(ch - 'a' + 'A');
                }

                char s[2] = {ch, 0};
                float fs = ImGui::GetFontSize() * 2.f;
                ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, s);

                dl->AddText(ImGui::GetFont(), fs, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), themeColorU32(c.theme.neutral[9]), s);
            }

            ImGui::PopID();
        };

        auto grid = [&](long base, long count) {
            for (long k = 0; k < count; k++) {
                if (k % cols) {
                    ImGui::SameLine();
                }

                cellItem(base + k);
            }
        };

        // bottom-up: the input line, then (group name, group content,
        // delimiter) per group — so top-down each grid carries its header
        // underneath, and the topmost group has no leading delimiter
        auto groupH = [&](long count) {
            return count ? (float)((count + cols - 1) / cols) * (cell + st.ItemSpacing.y) + ImGui::GetFontSize() + st.ItemSpacing.y : 0.f;
        };

        float contentH = groupH(appsN) + groupH(sysN) +
            (appsN && sysN ? st.ItemSpacing.y * 2.f + 1.f : 0.f);
        float childH = fminf(contentH, (float)screenH * 0.55f);

        if (n) {
            ImGui::BeginChild("##groups", ImVec2(0.f, childH));

            if (appsN) {
                grid(0, appsN);
                ImGui::TextDisabled("applications");
            }

            if (appsN && sysN) {
                ImGui::Separator();
            }

            if (sysN) {
                grid(appsN, sysN);
                ImGui::TextDisabled("system");
            }

            ImGui::EndChild();
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
