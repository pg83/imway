#include "launcher.h"
#include "scene.h"
#include "util.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <imgui.h>
#include <lunasvg.h>

#include <std/alg/qsort.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sys/fd.h>

using namespace stl;

namespace {
    constexpr int kIconPx = 48;

    // plain arrays: entries live in a Vector, which wants trivial types
    struct Entry {
        char name[128] = "";
        char exec[256] = "";
        char icon[256] = "";
        u64 tex = 0; // 0 = not loaded yet, 1 = load failed
    };

    void setStr(char* dst, size_t cap, StringView v) {
        size_t n = v.length() < cap - 1 ? v.length() : cap - 1;

        memcpy(dst, v.data(), n);
        dst[n] = 0;
    }

    struct LauncherImpl: public Launcher {
        Scene* scene = nullptr;
        IconHost* icons = nullptr;

        bool open = false;
        bool focusField = false;
        char query[256] = "";
        long sel = 0;
        Vector<Entry> entries;
        Vector<u32> vis;

        LauncherImpl(Scene& scn, IconHost& host) : scene(&scn), icons(&host) {
        }

        void toggle() override {
            open = !open;

            if (open) {
                query[0] = 0;
                sel = 0;
                focusField = true;
                rescan();
            }

            scene->needsFrame = true;
        }

        bool isOpen() const override {
            return open;
        }

        void forEachDataDir(auto&& fn) {
            if (const char* home = getenv("XDG_DATA_HOME")) {
                fn(StringView(home));
            } else if (const char* h = getenv("HOME")) {
                // fn formats paths of its own: this one needs its own builder
                StringBuilder p;

                p << h << "/.local/share"_sv;
                fn(sv(p));
            }

            const char* xdg = getenv("XDG_DATA_DIRS");
            StringView rest(xdg ? xdg : "/usr/local/share:/usr/share");

            while (!rest.empty()) {
                StringView one, tail;

                if (!rest.split(':', one, tail)) {
                    one = rest;
                    tail = {};
                }

                if (!one.empty()) {
                    fn(one);
                }

                rest = tail;
            }
        }

        void rescan() {
            entries.clear();

            forEachDataDir([this](StringView base) {
                // parseDesktop formats via the shared builder underneath,
                // the directory paths live across those calls
                StringBuilder dir;

                dir << base << "/applications"_sv;

                DIR* d = opendir(dir.cStr());

                if (!d) {
                    return;
                }

                while (dirent* de = readdir(d)) {
                    StringView n(de->d_name);

                    if (!n.endsWith(".desktop"_sv)) {
                        continue;
                    }

                    StringBuilder f;

                    f << sv(dir) << "/"_sv << n;
                    parseDesktop(f.cStr());
                }

                closedir(d);
            });

            quickSort(entries.mutData(), entries.mutData() + entries.length(), [](const Entry& a, const Entry& b) {
                return StringView(a.name) < StringView(b.name);
            });
        }

        void parseDesktop(const char* file) {
            ScopedFD fd(::open(file, O_RDONLY | O_CLOEXEC));

            if (fd.get() < 0) {
                return;
            }

            Vector<u8> data;
            u8 buf[4096];

            for (;;) {
                ssize_t n = read(fd.get(), buf, sizeof(buf));

                if (n <= 0) {
                    break;
                }

                data.append(buf, (size_t)n);
            }

            Entry en;
            bool inSection = false;
            bool display = true;
            bool isApp = false;
            StringView rest((const u8*)data.data(), data.length());

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

                if (key == "Name"_sv && !en.name[0]) {
                    setStr(en.name, sizeof(en.name), val);
                } else if (key == "Exec"_sv && !en.exec[0]) {
                    setExec(en, val);
                } else if (key == "Icon"_sv && !en.icon[0]) {
                    setStr(en.icon, sizeof(en.icon), val);
                } else if (key == "Type"_sv) {
                    isApp = val == "Application"_sv;
                } else if ((key == "NoDisplay"_sv || key == "Hidden"_sv) && val == "true"_sv) {
                    display = false;
                }
            }

            if (isApp && display && en.name[0] && en.exec[0]) {
                entries.pushBack(en);
            }
        }

        // strip the %f/%u/... field codes from Exec
        static void setExec(Entry& en, StringView val) {
            auto& out = sb();
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
            setStr(en.exec, sizeof(en.exec), sv(out));
        }

        u64 icon(Entry& e) {
            if (e.tex) {
                return e.tex == 1 ? 0 : e.tex;
            }

            e.tex = 1;

            StringView ic(e.icon);

            if (ic.empty()) {
                return 0;
            }

            if (ic[0] == '/') {
                if (ic.endsWith(".svg"_sv)) {
                    loadSvg(e, e.icon);
                }
            } else {
                forEachDataDir([this, &e, ic](StringView base) {
                    if (e.tex != 1) {
                        return;
                    }

                    auto& p = sb();

                    p << base << "/icons/hicolor/scalable/apps/"_sv << ic << ".svg"_sv;

                    if (access(p.cStr(), R_OK) == 0) {
                        loadSvg(e, p.cStr());
                    }
                });
            }

            return e.tex == 1 ? 0 : e.tex;
        }

        void loadSvg(Entry& e, const char* path) {
            auto doc = lunasvg::Document::loadFromFile(path);

            if (!doc) {
                return;
            }

            lunasvg::Bitmap bmp = doc->renderToBitmap(kIconPx, kIconPx);

            if (bmp.isNull()) {
                return;
            }

            // lunasvg bitmaps are premultiplied ARGB32, same as our textures
            if (u64 tex = icons->iconTexture((const u32*)bmp.data(), kIconPx, kIconPx)) {
                e.tex = tex;
            }
        }

        void refilter() {
            vis.clear();

            StringView q(query);
            u8 qbuf[256];
            StringView ql = q.lower(qbuf);

            for (size_t i = 0; i < entries.length(); i++) {
                u8 nbuf[128];
                StringView nl = StringView(entries[i].name).lower(nbuf);

                if (ql.empty() || nl.search(ql)) {
                    vis.pushBack((u32)i);
                }
            }
        }

        void run(const char* cmd) {
            if (!cmd[0] || !scene->socketName) {
                return;
            }

            pid_t pid = fork();

            if (pid == 0) {
                // double fork: the command reparents to init, no zombies
                if (fork() != 0) {
                    _exit(0);
                }

                setenv("WAYLAND_DISPLAY", scene->socketName, 1);
                execlp("sh", "sh", "-c", cmd, (char*)nullptr);
                _exit(127);
            }

            if (pid > 0) {
                waitpid(pid, nullptr, 0);
            }
        }

        void draw(int screenW, int screenH, float uiScale) override;
    };
}

void LauncherImpl::draw(int screenW, int screenH, float uiScale) {
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
            Entry& e = entries.mut(vis[(size_t)i]);
            bool selected = sel == i + 1;

            ImGui::PushID((int)vis[(size_t)i]);

            if (ImGui::Selectable("##row", selected, 0, ImVec2(0.f, rowH))) {
                run(e.exec);
                open = false;
            }

            if (selected && navved) {
                ImGui::SetScrollHereY(0.5f);
            }

            ImGui::SameLine(ImGui::GetStyle().FramePadding.x);

            if (u64 tex = icon(e)) {
                ImGui::Image((ImTextureID)tex, ImVec2(rowH, rowH));
            } else {
                ImGui::Dummy(ImVec2(rowH, rowH));
            }

            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFontSize()) / 2.f);
            ImGui::TextUnformatted(e.name);
            ImGui::PopID();
        }

        if (enter) {
            if (sel >= 1 && sel <= n) {
                run(entries.mut(vis[(size_t)(sel - 1)]).exec);
            } else {
                run(query);
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

Launcher* Launcher::create(ObjPool* pool, Scene& scene, IconHost& icons) {
    return pool->make<LauncherImpl>(scene, icons);
}
