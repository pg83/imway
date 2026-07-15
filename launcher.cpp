#include "launcher.h"
#include "icon_store.h"
#include "util.h"
#include "xdg_utils.h"

#include <imgui.h>

#include <std/alg/qsort.h>
#include <std/ios/fs_utils.h>
#include <std/lib/vector.h>
#include <std/sys/fs.h>

using namespace stl;

namespace {
    // one .desktop entry; the strings live in the dialog blob as offsets, so
    // rows stay trivial for stl::Vector
    struct Row {
        u32 name = 0, nameLen = 0;
        u32 exec = 0, execLen = 0;
        u32 icon = 0, iconLen = 0;
    };

    // dialog-scoped storage: filled when the dialog opens, dropped when it
    // closes (capacity recycles between openings)
    struct Dialog {
        bool active = false;
        bool focusField = false;
        // raw buffer by imgui InputText contract
        char query[256] = "";
        long sel = 0;

        StringBuilder blob;
        Vector<Row> rows;
        Vector<u32> vis;
    };
}

static StringView rowView(const Dialog& d, u32 off, u32 len) {
    return StringView((const u8*)d.blob.data() + off, (size_t)len);
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

static void parseDesktop(Dialog& d, StringBuilder& file) {
    Buffer data;

    readFileContent(file, data);

    if (data.empty()) {
        return;
    }

    StringBuilder name, exec, icon;
    bool inSection = false;
    bool display = true;
    bool isApp = false;
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
        } else if ((key == "NoDisplay"_sv || key == "Hidden"_sv) && val == "true"_sv) {
            display = false;
        }
    }

    if (!isApp || !display || name.empty() || exec.empty()) {
        return;
    }

    Row r;

    r.name = (u32)d.blob.used();
    r.nameLen = (u32)name.used();
    d.blob << sv(name);
    r.exec = (u32)d.blob.used();
    r.execLen = (u32)exec.used();
    d.blob << sv(exec);
    r.icon = (u32)d.blob.used();
    r.iconLen = (u32)icon.used();
    d.blob << sv(icon);
    d.rows.pushBack(r);
}

static void rescan(Dialog& d) {
    forEachXdgDataDir([&d](StringView base) {
        StringBuilder dir;

        dir << base << "/applications"_sv;

        // missing xdg dirs are normal: listDir throws, skip them
        try {
            listDir(sv(dir), [&d, &dir](const TPathInfo& e) {
                if (e.isDir || !e.item.endsWith(".desktop"_sv)) {
                    return;
                }

                StringBuilder f;

                f << sv(dir) << "/"_sv << e.item;
                parseDesktop(d, f);
            });
        } catch (...) {
        }
    });

    quickSort(d.rows.mutData(), d.rows.mutData() + d.rows.length(), [&d](const Row& a, const Row& b) {
        return rowView(d, a.name, a.nameLen) < rowView(d, b.name, b.nameLen);
    });
}

static void refilter(Dialog& d) {
    d.vis.clear();

    StringView q(d.query);
    u8 qbuf[256];
    StringView ql = q.lower(qbuf);

    for (size_t i = 0; i < d.rows.length(); i++) {
        u8 nbuf[128];
        StringView nl = rowView(d, d.rows[i].name, d.rows[i].nameLen).lower(nbuf);

        if (ql.empty() || nl.search(ql)) {
            d.vis.pushBack((u32)i);
        }
    }
}

static void drop(Dialog& d) {
    d.active = false;
    d.rows.clear();
    d.vis.clear();
    d.blob.reset();
    d.blob.shrinkToFit();
}

bool drawLauncher(int screenW, int screenH, float uiScale, IconStore& icons, IconResolver& texes, bool& open, StringBuilder& run) {
    static Dialog d;

    if (!open) {
        return false;
    }

    if (!d.active) {
        d.active = true;
        d.focusField = true;
        d.query[0] = 0;
        d.sel = 0;
        rescan(d);
    }

    refilter(d);

    bool picked = false;
    long n = (long)d.vis.length();

    if (d.sel > n) {
        d.sel = n;
    }

    float lw = (float)screenW / 4.f < 320.f ? 320.f : (float)screenW / 4.f;

    ImGui::SetNextWindowPos(ImVec2((float)screenW / 2.f, (float)screenH / 4.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(lw, 0.f), ImVec2(lw, (float)screenH / 2.f));

    if (ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && d.sel < n) {
            d.sel++;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && d.sel > 0) {
            d.sel--;
        }

        ImGui::SetNextItemWidth(-1.f);

        if (d.focusField) {
            ImGui::SetKeyboardFocusHere();
            d.focusField = false;
        }

        bool enter = ImGui::InputText("##q", d.query, sizeof(d.query), ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::IsItemEdited()) {
            d.sel = 0;
        }

        float rowH = ImGui::GetFontSize() * 1.7f;
        bool navved = ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);

        for (long i = 0; i < n; i++) {
            const Row& r = d.rows[d.vis[(size_t)i]];
            bool selected = d.sel == i + 1;

            ImGui::PushID((int)d.vis[(size_t)i]);

            if (ImGui::Selectable("##row", selected, 0, ImVec2(0.f, rowH))) {
                run << rowView(d, r.exec, r.execLen);
                picked = true;
                open = false;
            }

            if (selected && navved) {
                ImGui::SetScrollHereY(0.5f);
            }

            ImGui::SameLine(ImGui::GetStyle().FramePadding.x);

            if (u64 tex = texes.iconTexture(icons.forIconValue(rowView(d, r.icon, r.iconLen)))) {
                ImGui::Image((ImTextureID)tex, ImVec2(rowH, rowH));
            } else {
                ImGui::Dummy(ImVec2(rowH, rowH));
            }

            StringView name = rowView(d, r.name, r.nameLen);

            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFontSize()) / 2.f);
            ImGui::TextUnformatted((const char*)name.begin(), (const char*)name.end());
            ImGui::PopID();
        }

        if (enter && !picked) {
            if (d.sel >= 1 && d.sel <= n) {
                run << rowView(d, d.rows[d.vis[(size_t)(d.sel - 1)]].exec, d.rows[d.vis[(size_t)(d.sel - 1)]].execLen);
            } else {
                run << StringView(d.query);
            }

            picked = !run.empty();
            open = false;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            open = false;
        }
    }

    ImGui::End();

    if (!open) {
        drop(d);
    }

    return picked;
}
