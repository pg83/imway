#include "launcher.h"
#include "icon_store.h"
#include "scene.h"
#include "util.h"
#include "xdg_utils.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <imgui.h>

#include <std/alg/qsort.h>
#include <std/ios/fs_utils.h>
#include <std/lib/vector.h>
#include <std/sys/fs.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>
#include <std/sys/fd.h>

using namespace stl;

namespace {
    constexpr int kIconPx = 48;

    // plain arrays: entries live in a Vector, which wants trivial types
    struct Entry {
        StringBuilder name;
        StringBuilder exec;
        StringBuilder icon;
    };

    struct LauncherImpl: public Launcher {
        Scene* scene = nullptr;
        IconStore* icons = nullptr;

        bool open = false;
        bool focusField = false;
        // raw buffer by imgui InputText contract
        char query[256] = "";
        long sel = 0;

        // pool-backed: stl::Vector wants trivial elements
        ObjList<Entry> entryAlloc;
        Vector<Entry*> entries;
        Vector<u32> vis;

        LauncherImpl(ObjPool* pool, Scene& scn, IconStore& store);

        void toggle() override;
        bool isOpen() const override;
        void rescan();
        void parseDesktop(StringBuilder& file);

        // strip the %f/%u/... field codes from Exec
        void setExec(Entry& en, StringView val);

        void refilter();
        void run(StringView cmd);

        void draw(int screenW, int screenH, float uiScale, IconResolver& texes) override;
    };
}

LauncherImpl::LauncherImpl(ObjPool* pool, Scene& scn, IconStore& store)
    : scene(&scn)
    , icons(&store)
    , entryAlloc(pool)
{
}

void LauncherImpl::toggle() {
    open = !open;

    if (open) {
        query[0] = 0;
        sel = 0;
        focusField = true;
        rescan();
    }

    scene->needsFrame = true;
}

bool LauncherImpl::isOpen() const {
    return open;
}

void LauncherImpl::rescan() {
    for (Entry* e : entries) {
        entryAlloc.release(e);
    }

    entries.clear();

    forEachXdgDataDir([this](StringView base) {
        StringBuilder dir;

        dir << base << "/applications"_sv;

        listDir(sv(dir), [this, &dir](const TPathInfo& e) {
            if (e.isDir || !e.item.endsWith(".desktop"_sv)) {
                return;
            }

            StringBuilder f;

            f << sv(dir) << "/"_sv << e.item;
            parseDesktop(f);
        });
    });

    quickSort(entries.mutData(), entries.mutData() + entries.length(), [](const Entry* a, const Entry* b) {
        return sv(a->name) < sv(b->name);
    });
}

void LauncherImpl::parseDesktop(StringBuilder& file) {
    Buffer data;

    readFileContent(file, data);

    if (data.empty()) {
        return;
    }

    Entry* en = entryAlloc.make();
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

        if (key == "Name"_sv && en->name.empty()) {
            en->name << val;
        } else if (key == "Exec"_sv && en->exec.empty()) {
            setExec(*en, val);
        } else if (key == "Icon"_sv && en->icon.empty()) {
            en->icon << val;
        } else if (key == "Type"_sv) {
            isApp = val == "Application"_sv;
        } else if ((key == "NoDisplay"_sv || key == "Hidden"_sv) && val == "true"_sv) {
            display = false;
        }
    }

    if (isApp && display && !en->name.empty() && !en->exec.empty()) {
        entries.pushBack(en);
    } else {
        entryAlloc.release(en);
    }
}

void LauncherImpl::setExec(Entry& en, StringView val) {
    const u8* b = val.begin();
    const u8* seg = b;

    while (b < val.end()) {
        if (*b == '%' && b + 1 < val.end()) {
            en.exec << StringView(seg, b);
            b += 2;
            seg = b;
        } else {
            b++;
        }
    }

    en.exec << StringView(seg, b);
}

void LauncherImpl::refilter() {
    vis.clear();

    StringView q(query);
    u8 qbuf[256];
    StringView ql = q.lower(qbuf);

    for (size_t i = 0; i < entries.length(); i++) {
        u8 nbuf[128];
        StringView nl = sv(entries[i]->name).lower(nbuf);

        if (ql.empty() || nl.search(ql)) {
            vis.pushBack((u32)i);
        }
    }
}

void LauncherImpl::run(StringView cmd) {
    if (cmd.empty() || scene->socketName.empty()) {
        return;
    }

    // materialize before the fork, both live until the exec
    Buffer c(cmd), sock(scene->socketName);
    pid_t pid = fork();

    if (pid == 0) {
        // double fork: the command reparents to init, no zombies
        if (fork() != 0) {
            _exit(0);
        }

        setenv("WAYLAND_DISPLAY", sock.cStr(), 1);
        execlp("sh", "sh", "-c", c.cStr(), (char*)nullptr);
        _exit(127);
    }

    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

void LauncherImpl::draw(int screenW, int screenH, float uiScale, IconResolver& texes) {
    if (!open) {
        return;
    }

    refilter();

    long n = (long)vis.length();

    if (sel > n) {
        sel = n;
    }

    float lw = (float)screenW / 4.f < 320.f ? 320.f : (float)screenW / 4.f;

    ImGui::SetNextWindowPos(ImVec2((float)screenW / 2.f, (float)screenH / 4.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(lw, 0.f), ImVec2(lw, (float)screenH / 2.f));

    if (ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
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
            Entry& e = *entries[vis[(size_t)i]];
            bool selected = sel == i + 1;

            ImGui::PushID((int)vis[(size_t)i]);

            if (ImGui::Selectable("##row", selected, 0, ImVec2(0.f, rowH))) {
                run(sv(e.exec));
                open = false;
            }

            if (selected && navved) {
                ImGui::SetScrollHereY(0.5f);
            }

            ImGui::SameLine(ImGui::GetStyle().FramePadding.x);

            if (u64 tex = texes.iconTexture(icons->forIconValue(sv(e.icon)))) {
                ImGui::Image((ImTextureID)tex, ImVec2(rowH, rowH));
            } else {
                ImGui::Dummy(ImVec2(rowH, rowH));
            }

            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFontSize()) / 2.f);
            ImGui::TextUnformatted(e.name.cStr());
            ImGui::PopID();
        }

        if (enter) {
            if (sel >= 1 && sel <= n) {
                run(sv(entries[vis[(size_t)(sel - 1)]]->exec));
            } else {
                run(StringView(query));
            }

            open = false;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            open = false;
        }
    }

    ImGui::End();

    scene->needsFrame = true;
}

Launcher* Launcher::create(ObjPool* pool, Scene& scene, IconStore& icons) {
    return pool->make<LauncherImpl>(pool, scene, icons);
}
