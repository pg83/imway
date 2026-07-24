#include "desktop.h"

#include "log.h"
#include "osd.h"
#include "icon.h"
#include "util.h"
#include "wifi.h"
#include "input.h"
#include "mixer.h"
#include "scene.h"
#include "theme.h"
#include "toast.h"
#include "device.h"
#include "dialog.h"
#include "output.h"
#include "history.h"
#include "wayland.h"
#include "wifi_ui.h"
#include "calendar.h"
#include "composer.h"
#include "imgui_wm.h"
#include "keyboard.h"
#include "launcher.h"
#include "listener.h"
#include "log_view.h"
#include "notifier.h"
#include "renderer.h"
#include "settings.h"
#include "weak_ptr.h"
#include "inspector.h"
#include "intr_list.h"
#include "anr_dialog.h"
#include "input_sink.h"
#include "lock_screen.h"
#include "icon_provider.h"
#include "desktop_chrome.h"
#include "main_supervisor.h"

#include <std/sys/fs.h>
#include <std/ios/sys.h>
#include <std/str/view.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/ios/fs_utils.h>
#include <std/mem/obj_pool.h>

#include <ev.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input-event-codes.h>
#include <xdg-shell-server-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

// The window manager half of the old renderer: everything the user sees
// and touches. GPU work reaches it only through the Renderer surface and
// the scene; the renderer calls build() once per frame between NewFrame
// and Render, and drawCursorShape() from its cursor-plane rasterizer.

using namespace stl;

namespace {
    struct DesktopImpl;

    struct CallDesktopVolume: Listener {
        DesktopImpl* parent;

        CallDesktopVolume(DesktopImpl* p);
        void onListen(void*) override;
    };

    struct CallDesktopWifi: Listener {
        DesktopImpl* parent;

        CallDesktopWifi(DesktopImpl* p);
        void onListen(void*) override;
    };
}

namespace {
    enum class Chord {
        screenshot,
        lock,
        launcher,
        inspector,
        altTabNext,
        altTabPrev,
    };

    struct ChordDef {
        u32 mask; // exact modifier state to match; kChordAnyMods matches any
        u32 sym;
        Chord id;
        StringView chord;
        StringView action;
    };

    constexpr u32 kChordAnyMods = ~0u;

    // the one table behind both chord dispatch and the settings keys page
    const ChordDef kChords[] = {
        {kChordAnyMods, XKB_KEY_Print, Chord::screenshot, "PrtSc"_sv, "save a screenshot"_sv},
        {kModLogo, XKB_KEY_l, Chord::lock, "Super+L"_sv, "lock the screen"_sv},
        {kModLogo, XKB_KEY_F2, Chord::launcher, "Super+F2"_sv, "application launcher"_sv},
        {kModLogo, XKB_KEY_F12, Chord::inspector, "Super+F12"_sv, "inspector"_sv},
        {kModAlt, XKB_KEY_Tab, Chord::altTabNext, "Alt+Tab"_sv, "next window"_sv},
        {kModAlt | kModShift, XKB_KEY_Tab, Chord::altTabPrev, "Alt+Shift+Tab"_sv, "previous window"_sv},
    };
}

namespace {
    ImGuiKey evdevToImGuiKey(u32 code) {
        switch (code) {
            case KEY_A:
                return ImGuiKey_A;
            case KEY_B:
                return ImGuiKey_B;
            case KEY_C:
                return ImGuiKey_C;
            case KEY_D:
                return ImGuiKey_D;
            case KEY_E:
                return ImGuiKey_E;
            case KEY_F:
                return ImGuiKey_F;
            case KEY_G:
                return ImGuiKey_G;
            case KEY_H:
                return ImGuiKey_H;
            case KEY_I:
                return ImGuiKey_I;
            case KEY_J:
                return ImGuiKey_J;
            case KEY_K:
                return ImGuiKey_K;
            case KEY_L:
                return ImGuiKey_L;
            case KEY_M:
                return ImGuiKey_M;
            case KEY_N:
                return ImGuiKey_N;
            case KEY_O:
                return ImGuiKey_O;
            case KEY_P:
                return ImGuiKey_P;
            case KEY_Q:
                return ImGuiKey_Q;
            case KEY_R:
                return ImGuiKey_R;
            case KEY_S:
                return ImGuiKey_S;
            case KEY_T:
                return ImGuiKey_T;
            case KEY_U:
                return ImGuiKey_U;
            case KEY_V:
                return ImGuiKey_V;
            case KEY_W:
                return ImGuiKey_W;
            case KEY_X:
                return ImGuiKey_X;
            case KEY_Y:
                return ImGuiKey_Y;
            case KEY_Z:
                return ImGuiKey_Z;
            case KEY_1:
                return ImGuiKey_1;
            case KEY_2:
                return ImGuiKey_2;
            case KEY_3:
                return ImGuiKey_3;
            case KEY_4:
                return ImGuiKey_4;
            case KEY_5:
                return ImGuiKey_5;
            case KEY_6:
                return ImGuiKey_6;
            case KEY_7:
                return ImGuiKey_7;
            case KEY_8:
                return ImGuiKey_8;
            case KEY_9:
                return ImGuiKey_9;
            case KEY_0:
                return ImGuiKey_0;
            case KEY_F1:
                return ImGuiKey_F1;
            case KEY_F2:
                return ImGuiKey_F2;
            case KEY_F3:
                return ImGuiKey_F3;
            case KEY_F4:
                return ImGuiKey_F4;
            case KEY_F5:
                return ImGuiKey_F5;
            case KEY_F6:
                return ImGuiKey_F6;
            case KEY_F7:
                return ImGuiKey_F7;
            case KEY_F8:
                return ImGuiKey_F8;
            case KEY_F9:
                return ImGuiKey_F9;
            case KEY_F10:
                return ImGuiKey_F10;
            case KEY_F11:
                return ImGuiKey_F11;
            case KEY_F12:
                return ImGuiKey_F12;
            case KEY_LEFT:
                return ImGuiKey_LeftArrow;
            case KEY_RIGHT:
                return ImGuiKey_RightArrow;
            case KEY_UP:
                return ImGuiKey_UpArrow;
            case KEY_DOWN:
                return ImGuiKey_DownArrow;
            case KEY_HOME:
                return ImGuiKey_Home;
            case KEY_END:
                return ImGuiKey_End;
            case KEY_PAGEUP:
                return ImGuiKey_PageUp;
            case KEY_PAGEDOWN:
                return ImGuiKey_PageDown;
            case KEY_INSERT:
                return ImGuiKey_Insert;
            case KEY_DELETE:
                return ImGuiKey_Delete;
            case KEY_BACKSPACE:
                return ImGuiKey_Backspace;
            case KEY_TAB:
                return ImGuiKey_Tab;
            case KEY_ENTER:
                return ImGuiKey_Enter;
            case KEY_KPENTER:
                return ImGuiKey_KeypadEnter;
            case KEY_ESC:
                return ImGuiKey_Escape;
            case KEY_SPACE:
                return ImGuiKey_Space;
            case KEY_MINUS:
                return ImGuiKey_Minus;
            case KEY_EQUAL:
                return ImGuiKey_Equal;
            case KEY_LEFTBRACE:
                return ImGuiKey_LeftBracket;
            case KEY_RIGHTBRACE:
                return ImGuiKey_RightBracket;
            case KEY_SEMICOLON:
                return ImGuiKey_Semicolon;
            case KEY_APOSTROPHE:
                return ImGuiKey_Apostrophe;
            case KEY_GRAVE:
                return ImGuiKey_GraveAccent;
            case KEY_BACKSLASH:
                return ImGuiKey_Backslash;
            case KEY_COMMA:
                return ImGuiKey_Comma;
            case KEY_DOT:
                return ImGuiKey_Period;
            case KEY_SLASH:
                return ImGuiKey_Slash;
            case KEY_CAPSLOCK:
                return ImGuiKey_CapsLock;
            case KEY_LEFTSHIFT:
                return ImGuiKey_LeftShift;
            case KEY_RIGHTSHIFT:
                return ImGuiKey_RightShift;
            case KEY_LEFTCTRL:
                return ImGuiKey_LeftCtrl;
            case KEY_RIGHTCTRL:
                return ImGuiKey_RightCtrl;
            case KEY_LEFTALT:
                return ImGuiKey_LeftAlt;
            case KEY_RIGHTALT:
                return ImGuiKey_RightAlt;
            case KEY_LEFTMETA:
                return ImGuiKey_LeftSuper;
            case KEY_RIGHTMETA:
                return ImGuiKey_RightSuper;
            default:
                return ImGuiKey_None;
        }
    }
}

namespace {
    // vector mouse cursor: the atlas-baked imgui one is a 12x19 bitmap and turns
    // to mush when scaled, so draw crisp shapes at any --scale instead
    void cursorPoly(ImDrawList* dl, const ImVec2* src, int n, ImVec2 p, float s, float rc, float rs) {
        ImVec2 pts[16], sh[16];

        for (int i = 0; i < n; i++) {
            float x = src[i].x * rc - src[i].y * rs;
            float y = src[i].x * rs + src[i].y * rc;

            pts[i] = ImVec2(p.x + x * s, p.y + y * s);
            sh[i] = ImVec2(pts[i].x + 1.5f * s, pts[i].y + 1.5f * s);
        }

        dl->AddConcavePolyFilled(sh, n, IM_COL32(0, 0, 0, 48));
        dl->AddConcavePolyFilled(pts, n, IM_COL32_WHITE);
        dl->AddPolyline(pts, n, IM_COL32_BLACK, ImDrawFlags_Closed, s > 1.f ? 1.2f * s : 1.2f);
    }

    void cursorStroke(ImDrawList* dl, ImVec2 p, float s, float x0, float y0, float x1, float y1, ImU32 col, float th) {
        dl->AddLine(ImVec2(p.x + x0 * s, p.y + y0 * s), ImVec2(p.x + x1 * s, p.y + y1 * s), col, th * s);
    }

    void drawMouseCursor(ImDrawList* dl, ImVec2 p, float s, ImGuiMouseCursor c) {
        // NB: AddConcavePolyFilled wants this winding, reversed it fills the convex hull
        static const ImVec2 arrow[] = {{12.2f, 11.8f}, {6.8f, 11.8f}, {9.5f, 17.8f}, {6.9f, 18.9f}, {4.2f, 12.9f}, {0.f, 16.5f}, {0.f, 0.f}};
        static const ImVec2 ns[] = {{0.f, -9.f}, {4.5f, -4.5f}, {1.7f, -4.5f}, {1.7f, 4.5f}, {4.5f, 4.5f}, {0.f, 9.f}, {-4.5f, 4.5f}, {-1.7f, 4.5f}, {-1.7f, -4.5f}, {-4.5f, -4.5f}};
        static const ImVec2 hand[] = {{0.f, 0.f}, {2.6f, 0.f}, {2.6f, 5.5f}, {4.f, 4.f}, {6.f, 4.f}, {7.6f, 5.6f}, {7.6f, 10.f}, {5.6f, 14.5f}, {-0.5f, 14.5f}, {-3.4f, 10.5f}, {-3.4f, 6.5f}, {-1.6f, 4.8f}, {0.f, 6.f}};
        const float R = 0.70710678f;

        switch (c) {
            case ImGuiMouseCursor_None:
                return;
            case ImGuiMouseCursor_TextInput:
                cursorStroke(dl, p, s, -2.5f, -8.f, 2.5f, -8.f, IM_COL32_BLACK, 3.4f);
                cursorStroke(dl, p, s, 0.f, -8.f, 0.f, 8.f, IM_COL32_BLACK, 3.4f);
                cursorStroke(dl, p, s, -2.5f, 8.f, 2.5f, 8.f, IM_COL32_BLACK, 3.4f);
                cursorStroke(dl, p, s, -2.5f, -8.f, 2.5f, -8.f, IM_COL32_WHITE, 1.4f);
                cursorStroke(dl, p, s, 0.f, -8.f, 0.f, 8.f, IM_COL32_WHITE, 1.4f);
                cursorStroke(dl, p, s, -2.5f, 8.f, 2.5f, 8.f, IM_COL32_WHITE, 1.4f);
                return;
            case ImGuiMouseCursor_ResizeNS:
                cursorPoly(dl, ns, 10, p, s, 1.f, 0.f);
                return;
            case ImGuiMouseCursor_ResizeEW:
                cursorPoly(dl, ns, 10, p, s, 0.f, 1.f);
                return;
            case ImGuiMouseCursor_ResizeNESW:
                cursorPoly(dl, ns, 10, p, s, R, R);
                return;
            case ImGuiMouseCursor_ResizeNWSE:
                cursorPoly(dl, ns, 10, p, s, R, -R);
                return;
            case ImGuiMouseCursor_ResizeAll:
                cursorPoly(dl, ns, 10, p, s, 1.f, 0.f);
                cursorPoly(dl, ns, 10, p, s, 0.f, 1.f);
                return;
            case ImGuiMouseCursor_Hand:
                cursorPoly(dl, hand, 13, p, s, 1.f, 0.f);
                return;
            case ImGuiMouseCursor_NotAllowed:
                dl->AddCircle(p, 7.5f * s, IM_COL32_BLACK, 0, 4.f * s);
                dl->AddCircle(p, 7.5f * s, IM_COL32_WHITE, 0, 2.f * s);
                cursorStroke(dl, p, s, -5.3f, -5.3f, 5.3f, 5.3f, IM_COL32_BLACK, 4.f);
                cursorStroke(dl, p, s, -5.3f, -5.3f, 5.3f, 5.3f, IM_COL32_WHITE, 2.f);
                return;
            case ImGuiMouseCursor_Wait:
            case ImGuiMouseCursor_Progress: {
                float a0 = (float)(nowMsec() % 1000) * 0.0062831853f;
                float a1 = a0 + 5.2f;

                if (c == ImGuiMouseCursor_Progress) {
                    cursorPoly(dl, arrow, 7, p, s, 1.f, 0.f);
                    p = ImVec2(p.x + 14.f * s, p.y - 1.f * s);
                }

                dl->PathArcTo(p, 6.5f * s, a0, a1);
                dl->PathStroke(IM_COL32_BLACK, ImDrawFlags_None, 3.6f * s);
                dl->PathArcTo(p, 6.5f * s, a0, a1);
                dl->PathStroke(IM_COL32_WHITE, ImDrawFlags_None, 1.8f * s);
                return;
            }
            default:
                cursorPoly(dl, arrow, 7, p, s, 1.f, 0.f);
                return;
        }
    }
}

namespace {
    StringView readSmallFile(Buffer& path, Buffer& out) {
        out.reset();
        readFileContent(path, out);

        return sv(out);
    }

}

namespace {
    struct DesktopImpl: Desktop, InputSink, Listener {
        Composer* comp = nullptr;
        Scene* scene = nullptr;
        ::Output* output = nullptr;
        Renderer* renderer = nullptr;
        Keyboard* keyboard = nullptr;
        Notifier* notifier = nullptr;

        float uiScale = 1.f;
        float nextUiScale = 1.f;
        u64 themeRevision = 0;
        ThemeColor desktopColor;
        Settings settings;
        DialogState* settingsState = nullptr;
        DialogState* anrState = nullptr;
        bool anrToggle = false;
        Weak<Toplevel> anrTarget;
        bool settingsToggle = false;
        // the keys page rows: the chord table plus the non-chord bindings
        Vector<KeyBinding> bindingsView;

        // bar battery widget, /sys-fed; sampled at most once per ~2s
        u64 statMs = 0;
        long batPct = -1; // -1 no battery
        bool batDischarging = false;
        StringBuilder batPath;

        DialogState* calendarState = nullptr;
        bool calendarToggle = false;

        // osd: armed by mixer/backlight changes, fades at the tail
        u64 osdMs = 0;
        int osdKind = 0; // 1 volume, 2 brightness

        DialogState* wifiState = nullptr;
        bool wifiToggle = false;
        DialogState* inspectorState = nullptr;
        bool inspectorToggle = false;
        DialogState* historyState = nullptr;
        bool historyToggle = false;
        DialogState* logState = nullptr;
        bool logToggle = false;

        // color picker (eyedropper): armed from the launcher, the next
        // click samples the framebuffer pixel under the cursor
        bool pickArmed = false;
        bool pickShow = false;
        u8 pickR = 0, pickG = 0, pickB = 0;

        // the cursor position lives here: sources emit raw deltas, this is
        // the code that integrates them and applies lock/confine policy
        double posX = 0, posY = 0;
        bool placed = false;
        bool kbCapturePrev = false;
        bool chordDown[256] = {};

        // alt-tab overlay: selection commits on Alt release; weak so a
        // window dying under the overlay drops out of the selection
        bool altTabActive = false;
        Weak<Toplevel> altTabSel;

        // lockscreen's self-owned arena also gates the input sink
        DialogState* lockState = nullptr;

        DialogState* launcherState = nullptr;
        bool launcherToggle = false;
        float launcherX = -1.f, launcherY = -1.f;

        DesktopImpl(Composer& c, float scale);
        ~DesktopImpl() noexcept;

        void build() override;
        void drawCursorShape(ImDrawList* dl, const ImVec2& pos, float scale, int kind) override;

        // Composer::outputResizedListeners: place or clamp the pointer
        void onListen(void*) override;

        void buildUi(Scene& scene);
        void cursorUi(Scene& scene, bool overClient);
        void sampleStats();
        void volumeChanged();
        void wifiChanged();
        void markTreeUnhovered(Surface& s);
        void clampPos();
        bool toastsActive() const;

        bool pointerMotion(PointerMotionEvent& ev) override;
        bool button(u32 btn, bool pressed) override;
        bool key(u32 code, bool pressed) override;
        bool scroll(const ScrollEvent& ev) override;
        bool tabletTool(const TabletToolEvent& ev) override;
        bool swipeBegin(u32 fingers) override;
        bool swipeUpdate(double dx, double dy) override;
        bool swipeEnd(bool cancelled) override;
        bool pinchBegin(u32 fingers) override;
        bool pinchUpdate(double dx, double dy, double scale, double rotation) override;
        bool pinchEnd(bool cancelled) override;
        bool holdBegin(u32 fingers) override;
        bool holdEnd(bool cancelled) override;

        bool chordAction(u32 mask, u32 sym);
        void altTabStep(long dir);
        void altTabCommit();
    };

    CallDesktopVolume::CallDesktopVolume(DesktopImpl* p)
        : parent(p)
    {
    }

    void CallDesktopVolume::onListen(void*) {
        parent->volumeChanged();
    }

    CallDesktopWifi::CallDesktopWifi(DesktopImpl* p)
        : parent(p)
    {
    }

    void CallDesktopWifi::onListen(void*) {
        parent->wifiChanged();
    }
}

void DesktopImpl::markTreeUnhovered(Surface& s) {
    s.hovered = false;

    forEach<Subsurface>(s.stackBelow, [&](Subsurface& c) {
        if (c.surface) {
            markTreeUnhovered(*c.surface);
        }
    });

    forEach<Subsurface>(s.stackAbove, [&](Subsurface& c) {
        if (c.surface) {
            markTreeUnhovered(*c.surface);
        }
    });
}

void DesktopImpl::drawCursorShape(ImDrawList* dl, const ImVec2& pos, float scale, int kind) {
    drawMouseCursor(dl, pos, scale, kind);
}

void DesktopImpl::clampPos() {
    posX = posX < 0 ? 0 : posX >= scene->outW ? scene->outW - 1 : posX;
    posY = posY < 0 ? 0 : posY >= scene->outH ? scene->outH - 1 : posY;

    if (!scene->pointerConfined || scene->confineRegion.empty()) {
        return;
    }

    double bestX = posX, bestY = posY;
    double bestDist = -1;

    for (const RectI& r : scene->confineRegion) {
        double x0 = r.x, y0 = r.y;
        double x1 = (double)r.x + r.w - 1;
        double y1 = (double)r.y + r.h - 1;
        double x = posX < x0 ? x0 : posX > x1 ? x1 : posX;
        double y = posY < y0 ? y0 : posY > y1 ? y1 : posY;
        double dx = posX - x, dy = posY - y;
        double dist = dx * dx + dy * dy;

        if (bestDist < 0 || dist < bestDist) {
            bestDist = dist;
            bestX = x;
            bestY = y;
        }
    }

    posX = bestX;
    posY = bestY;
}

bool DesktopImpl::pointerMotion(PointerMotionEvent& ev) {
    if (ev.kind == PointerMotionKind::relative) {
        if (!scene->pointerLocked) {
            ev.x = posX + ev.dx;
            ev.y = posY + ev.dy;
            ev.moved = true;
        }
    } else if (ev.kind == PointerMotionKind::absolute) {
        if (!scene->pointerLocked) {
            ev.x *= scene->outW;
            ev.y *= scene->outH;
            ev.moved = true;
        }
    } else {
        ev.moved = true;
    }

    if (ev.moved) {
        posX = ev.x;
        posY = ev.y;
        clampPos();
        ev.x = posX;
        ev.y = posY;
        scene->needsFrame = true;
        ImGui::GetIO().AddMousePosEvent((float)posX, (float)posY);

        renderer->cursorPlaneMove(posX, posY);
    }

    return lockState != nullptr;
}

bool DesktopImpl::button(u32 btn, bool pressed) {
    scene->needsFrame = true;

    if (btn == BTN_LEFT || btn == BTN_RIGHT || btn == BTN_MIDDLE) {
        ImGui::GetIO().AddMouseButtonEvent(btn == BTN_LEFT ? 0 : btn == BTN_RIGHT ? 1 : 2, pressed);
    }

    if (lockState) {
        return pressed;
    }

    if (pickArmed && pressed && btn == BTN_LEFT) {
        pickArmed = false;

        // the picker being armed kept composition on, so the last frame is
        // readable; one synchronous readback on a click is fine
        if (renderer->readPixel((int)posX, (int)posY, pickR, pickG, pickB)) {
            pickShow = true;

            if (notifier) {
                static const char hx[] = "0123456789abcdef";
                char h[8] = {'#', hx[pickR >> 4], hx[pickR & 15], hx[pickG >> 4], hx[pickG & 15], hx[pickB >> 4], hx[pickB & 15], 0};

                Post p;

                p.app = "color picker"_sv;
                p.summary = "color picked"_sv;
                p.body = StringView(h); // copied by post() before h dies
                notifier->post(p);
            }
        }

        scene->needsFrame = true;

        return true;
    }

    return (btn == BTN_LEFT || btn == BTN_RIGHT || btn == BTN_MIDDLE) && pressed && scene->ptrCaptured;
}

bool DesktopImpl::scroll(const ScrollEvent& ev) {
    scene->needsFrame = true;
    ImGui::GetIO().AddMouseWheelEvent((float)-ev.dx, (float)-ev.dy);

    return lockState || scene->ptrCaptured;
}

bool DesktopImpl::tabletTool(const TabletToolEvent& ev) {
    // the pen drives the shared cursor, so hover picking follows it exactly
    // like the mouse — one frame behind. ImGui has no stylus model beyond
    // that: the events stay unconsumed and reach the client under the pen
    posX = ev.x;
    posY = ev.y;
    clampPos();
    scene->needsFrame = true;
    ImGui::GetIO().AddMousePosEvent((float)posX, (float)posY);

    renderer->cursorPlaneMove(posX, posY);

    return lockState != nullptr;
}

bool DesktopImpl::swipeBegin(u32) {
    return lockState != nullptr;
}

bool DesktopImpl::swipeUpdate(double, double) {
    return lockState != nullptr;
}

bool DesktopImpl::swipeEnd(bool) {
    return lockState != nullptr;
}

bool DesktopImpl::pinchBegin(u32) {
    return lockState != nullptr;
}

bool DesktopImpl::pinchUpdate(double, double, double, double) {
    return lockState != nullptr;
}

bool DesktopImpl::pinchEnd(bool) {
    return lockState != nullptr;
}

bool DesktopImpl::holdBegin(u32) {
    return lockState != nullptr;
}

bool DesktopImpl::holdEnd(bool) {
    return lockState != nullptr;
}

bool DesktopImpl::key(u32 code, bool pressed) {
    scene->needsFrame = true;

    if (keyboard) {
        keyboard->updateKey(code, pressed);
    }

    bool locked = lockState != nullptr;
    bool consumed = false;
    u32 mask = keyboard ? keyboard->modMask() : 0;

    if (!locked && pressed && (output->hasBrightness() || output->colorState().hdr()) && (code == KEY_BRIGHTNESSUP || code == KEY_BRIGHTNESSDOWN)) {
        bool up = code == KEY_BRIGHTNESSUP;

        if (output->colorState().hdr()) {
            output->setSdrWhite(output->colorState().sdrWhiteNits + (up ? 10.0 : -10.0));
            osdKind = 3;
        } else {
            output->setBrightness(output->brightness() + (up ? .05f : -.05f));
            osdKind = 2;
        }

        osdMs = nowMsec() + 1500;
        scene->needsFrame = true;
        consumed = true;
    }

    if (!locked && !consumed && pressed && comp->mixer) {
        Mixer* mixer = comp->mixer;

        if (code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN) {
            float delta = code == KEY_VOLUMEUP ? 0.05f : -0.05f;
            float volume = mixer->volume() + delta;

            mixer->setVolume(volume < 0.f ? 0.f : volume > 1.f ? 1.f : volume);
            volumeChanged();
            consumed = true;
        }

        if (!consumed && code == KEY_MUTE) {
            mixer->setMuted(!mixer->muted());
            volumeChanged();
            consumed = true;
        }
    }

    if (!locked && altTabActive && !pressed && (code == KEY_LEFTALT || code == KEY_RIGHTALT)) {
        altTabActive = false;
        altTabSel.reset();
        scene->needsFrame = true;
    }

    if (!locked && !consumed && !scene->shortcutsInhibited && keyboard && code < 256) {
        if (altTabActive && pressed && keyboard->keysymBase(code) == XKB_KEY_Escape) {
            altTabActive = false;
            altTabSel.reset();
            scene->needsFrame = true;
            chordDown[code] = true;
            consumed = true;
        }

        if (!consumed && pressed && chordAction(mask, keyboard->keysymBase(code))) {
            chordDown[code] = true;
            consumed = true;
        }

        if (!consumed && !pressed && chordDown[code]) {
            chordDown[code] = false;

            if (altTabActive && keyboard->keysymBase(code) == XKB_KEY_Tab) {
                altTabCommit();
            }

            consumed = true;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    bool capture = launcherState || altTabActive || io.WantTextInput;

    scene->kbCaptured = locked || capture;

    // Super+L creates the lock while this key is being handled. Do not feed
    // the activating L into the password field.
    if (!locked && lockState) {
        return true;
    }

    io.AddKeyEvent(ImGuiMod_Ctrl, mask & kModCtrl);
    io.AddKeyEvent(ImGuiMod_Shift, mask & kModShift);
    io.AddKeyEvent(ImGuiMod_Alt, mask & kModAlt);
    io.AddKeyEvent(ImGuiMod_Super, mask & kModLogo);

    if (ImGuiKey key = evdevToImGuiKey(code); key != ImGuiKey_None) {
        io.AddKeyEvent(key, pressed);
    }

    if (pressed && keyboard) {
        char buf[8];

        if (keyboard->utf8(code, buf, sizeof(buf)) > 0 && (u8)buf[0] >= 0x20) {
            io.AddInputCharactersUTF8(buf);
        }
    }

    if (locked) {
        return pressed;
    }

    return consumed || capture && pressed;
}

void DesktopImpl::wifiChanged() {
    scene->needsFrame = true;
}

void DesktopImpl::volumeChanged() {
    if (!settingsState) {
        osdMs = nowMsec() + 1500;
        osdKind = 1;
    }

    scene->needsFrame = true;
}

bool DesktopImpl::chordAction(u32 mask, u32 sym) {
    for (const ChordDef& d : kChords) {
        if (d.sym != sym || (d.mask != kChordAnyMods && d.mask != mask)) {
            continue;
        }

        switch (d.id) {
            case Chord::screenshot:
                renderer->captureScreenshot();
                break;
            case Chord::lock:
                openLockOverlay(*comp, &lockState);
                break;
            case Chord::launcher:
                launcherX = launcherY = -1.f;
                launcherToggle = true;
                scene->needsFrame = true;
                break;
            case Chord::inspector:
                inspectorToggle = true;
                scene->needsFrame = true;
                break;
            case Chord::altTabNext:
                altTabStep(1);
                break;
            case Chord::altTabPrev:
                altTabStep(-1);
                break;
        }

        return true;
    }

    return false;
}

void DesktopImpl::altTabStep(long dir) {
    IntrusiveList& tls = scene->toplevels;

    if (tls.empty()) {
        return;
    }

    Toplevel* base = altTabActive && altTabSel.get() ? altTabSel.get() : scene->focusedToplevel.get();
    // a circular walk over the ring; the base (or the head sentinel when
    // there is no base) both starts and bounds it, and gets re-tested last
    // so a lone mapped window still selects itself
    IntrusiveNode* start = base && !base->singular() ? (IntrusiveNode*)base : tls.mutEnd();
    IntrusiveNode* n = start;

    do {
        n = dir > 0 ? n->next : n->prev;

        if (n == tls.mutEnd()) {
            n = dir > 0 ? n->next : n->prev;
        }

        Toplevel* t = (Toplevel*)n;

        if (n != tls.mutEnd() && t->mapped) {
            altTabActive = true;
            altTabSel.bind(t->weak);
            scene->needsFrame = true;

            return;
        }
    } while (n != start);
}

void DesktopImpl::altTabCommit() {
    if (altTabSel.get() && altTabSel->mapped) {
        altTabSel->raiseRequested = true;
    }

    scene->needsFrame = true;
}

void DesktopImpl::sampleStats() {
    u64 now = nowMsec();

    if (statMs && now - statMs < 1900) {
        return;
    }

    statMs = now;

    Buffer content;

    // no battery picked (startup, or the last one vanished): re-enumerate.
    // system batteries win over scope=Device ones (hid keyboards/mice) —
    // those ride usb hotplug, so they are a fallback, not the first choice
    if (batPath.empty()) {
        StringBuilder devBat;

        try {
            listDir("/sys/class/power_supply"_sv, [this, &content, &devBat](const TPathInfo& e) {
                if (!batPath.empty()) {
                    return;
                }

                StringBuilder p;

                p << "/sys/class/power_supply/"_sv << e.item << "/type"_sv;

                if (!readSmallFile(p, content).startsWith("Battery"_sv)) {
                    return;
                }

                bool device = false;

                try {
                    p.reset();
                    p << "/sys/class/power_supply/"_sv << e.item << "/scope"_sv;
                    device = readSmallFile(p, content).startsWith("Device"_sv);
                } catch (...) {
                    // no scope file: a system battery
                }

                if (device) {
                    if (devBat.empty()) {
                        devBat << "/sys/class/power_supply/"_sv << e.item;
                    }
                } else {
                    batPath << "/sys/class/power_supply/"_sv << e.item;
                }
            });
        } catch (...) {
        }

        if (batPath.empty() && !devBat.empty()) {
            batPath << sv(devBat);
        }
    }

    batPct = -1;

    if (!batPath.empty()) {
        try {
            auto& p = sb();

            p << sv(batPath) << "/capacity"_sv;
            batPct = (long)readSmallFile(p, content).stou();
            p.reset();
            p << sv(batPath) << "/status"_sv;
            batDischarging = readSmallFile(p, content).startsWith("Discharging"_sv);
        } catch (...) {
            // the supply vanished under us (hub unplugged): forget it, the
            // next tick re-enumerates — and finds it again on replug
            batPath.reset();
        }
    }
}

// resizeAnchor bits: which edge stays under the hand during a drag
enum {
    kResizeLeft = 1,
    kResizeTop = 2,
    kResizeActive = 0x80
};

// transactional resize: a border/grip drag is a request, not a resize. the
// callback pins the window at the client's committed size (applyW/H) and
// records where the hand wants it (dragW/H) — that becomes a configure, and
// the window steps only when the client commits a matching buffer. anything
// that is not an active border/grip drag (initial sizing, our own applies)
// passes through untouched. it also records which edge is being dragged so the
// draw loop can grow toward the hand for left/top drags
static void toplevelSizeCb(ImGuiSizeCallbackData* d) {
    auto* t = (Toplevel*)d->UserData;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* w = g.CurrentWindow;

    if (!g.ActiveId || g.ActiveIdWindow != w) {
        return;
    }

    bool resizing = false;
    u8 anchor = 0;

    if (g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Left)) {
        resizing = true;
        anchor |= kResizeLeft;
    }

    if (g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Up)) {
        resizing = true;
        anchor |= kResizeTop;
    }

    if (g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Right) || g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Down)) {
        resizing = true;
    }

    // corner grips, in imgui's order: 0 bottom-right, 1 bottom-left, 2 top-left,
    // 3 top-right
    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 0)) {
        resizing = true;
    }

    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 1)) {
        resizing = true;
        anchor |= kResizeLeft;
    }

    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 2)) {
        resizing = true;
        anchor |= kResizeLeft | kResizeTop;
    }

    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 3)) {
        resizing = true;
        anchor |= kResizeTop;
    }

    if (!resizing) {
        return;
    }

    t->dragW = d->DesiredSize.x;
    t->dragH = d->DesiredSize.y;
    d->DesiredSize = ImVec2(t->applyW, t->applyH);

    // latch the anchor for the whole transaction (drag through client commit)
    if (!(t->resizeAnchor & kResizeActive)) {
        t->resizeAnchor = anchor | kResizeActive;
    }
}

static StringView terminalProgram() {
    const char* value = getenv("IMWAY_TERMINAL");

    if (!value || !*value) {
        value = getenv("TERMINAL");
    }

    return value && *value ? StringView(value) : "zutty"_sv;
}

static void spawnClient(Composer& comp, StringView cmd, StringView sock, bool terminal) {
    if (cmd.empty() || sock.empty()) {
        return;
    }

    StringView shellArgs[] = {"sh"_sv, "-c"_sv, cmd};
    StringView terminalArgs[] = {terminalProgram(), "-e"_sv, "sh"_sv, "-c"_sv, cmd};
    StringBuilder display;

    display << "WAYLAND_DISPLAY="_sv << sock;

    StringView env[] = {sv(display)};
    SupervisorSpawn spawn;

    spawn.args = terminal ? terminalArgs : shellArgs;
    spawn.argCount = terminal ? 5 : 3;
    spawn.env = env;
    spawn.envCount = 1;

    comp.supervisor->spawn(spawn);
}

static StringView wifiGlyph(WifiState s) {
    switch (s) {
        case WifiState::connected:
            return "wifi"_sv;
        case WifiState::connecting:
            return "wifi..."_sv;
        case WifiState::scanning:
            return "wifi.."_sv;
        case WifiState::disconnected:
            return "wifi off"_sv;
        case WifiState::unavailable:
            return "no wifi"_sv;
    }

    return "wifi"_sv;
}

void DesktopImpl::buildUi(Scene& scene) {
    if (scene.focusedToplevel && (scene.focusedToplevel->minimized || !scene.focusedToplevel->mapped)) {
        scene.focusedToplevel.reset();
    }

    if (themeRevision != comp->theme.revision) {
        applyImGuiTheme(ImGui::GetStyle(), comp->theme);
        themeRevision = comp->theme.revision;
        desktopColor = comp->theme.desktop;
    }

    // the ui only writes nextUiScale: the scale flips here and nowhere
    // else, so a whole frame never mixes the new scale with the old style
    if (nextUiScale != uiScale) {
        uiScale = nextUiScale;

        // restyle from a pristine copy: ScaleAllSizes compounds otherwise
        ImGuiStyle fresh;

        applyImGuiTheme(fresh, comp->theme);
        fresh.FontScaleMain = uiScale;
        fresh.ScaleAllSizes(uiScale);
        ImGui::GetStyle() = fresh;

        // the renderer rebakes its cursor bitmaps and the shadow sprite
        // off this scalar
        scene.uiScale = uiScale;
        scene.needsFrame = true;
    }

    sampleStats();

    DesktopChromeInfo chromeInfo;

    // last frame's truth: the reset happens right before the toplevels draw
    if (Toplevel* ft = scene.focusedToplevel.get()) {
        chromeInfo.focusedAppId = sv(ft->appId);
    }

    chromeInfo.layout = StringView(scene.layout);
    chromeInfo.wifi = comp->wifi ? wifiGlyph(comp->wifi->state()) : StringView();
    chromeInfo.batteryPct = batDischarging ? batPct : -1;

    DesktopChromeResult chromeResult;

    drawDesktopChrome(*comp, chromeInfo, chromeResult);

    if (chromeResult.launcher) {
        launcherX = chromeResult.launcherX;
        launcherY = chromeResult.launcherY;
        launcherToggle = true;
        scene.needsFrame = true;
    }

    calendarToggle |= chromeResult.calendar;
    wifiToggle |= chromeResult.wifi;

    drawCalendar(*comp, calendarToggle, &calendarState);
    calendarToggle = false;

    if (comp->wifi) {
        drawWifi(*comp, wifiToggle, &wifiState);
        wifiToggle = false;
    }

    if (notifier) {
        drawToasts(*comp, *notifier, *comp->iconResolver, scene.outW, uiScale);
    }

    if (settings.sdrNits < 0.f) {
        settings.sdrNits = (float)output->colorState().sdrWhiteNits;
    }

    settings.uiScale = uiScale;
    settings.neutral = comp->theme.neutralSeed;
    settings.selection = comp->theme.selectionSeed;

    if (comp->mixer) {
        settings.volume = comp->mixer->volume();
        settings.volMuted = comp->mixer->muted();
    } else {
        settings.volume = -1.f;
    }

    settings.brightness = output->hasBrightness() && !output->colorState().hdr() ? output->brightness() : -1.f;
    settings.hdrPeakNits = (float)output->colorState().displayPeakNits;
    settings.hasDnd = notifier != nullptr;

    if (notifier) {
        settings.dnd = notifier->dnd();
    }

    if (keyboard) {
        u32 n = keyboard->layoutCount();

        settings.layoutCount = n < Settings::kMaxLayouts ? n : Settings::kMaxLayouts;

        for (u32 i = 0; i < settings.layoutCount; i++) {
            settings.layouts[i] = keyboard->layoutName(i);
        }

        settings.layoutActive = keyboard->activeLayout();
        settings.xkbOptions = keyboard->options();
    } else {
        settings.layoutCount = 0;
    }

    settings.hasPointer = comp->input != nullptr;

    if (comp->input) {
        settings.pointerSpeed = (float)comp->input->pointerSpeed();
    }

    settings.bindings = bindingsView.data();
    settings.bindingCount = bindingsView.length();

    drawSettings(*comp, settings, settingsToggle, &settingsState);
    drawAnrDialog(*comp, anrTarget, anrToggle, &anrState);
    anrToggle = false;
    settingsToggle = false;

    if (settings.dndChanged && notifier) {
        notifier->setDnd(settings.dnd);
    }

    if (settings.volumeChanged && comp->mixer) {
        comp->mixer->setVolume(settings.volume);
    }

    if (settings.muteChanged && comp->mixer) {
        comp->mixer->setMuted(settings.volMuted);
    }

    if (settings.brightnessChanged) {
        output->setBrightness(settings.brightness);
    }

    if (settings.scaleChanged) {
        nextUiScale = settings.scale;
    }

    if (settings.sdrChanged) {
        output->setSdrWhite(settings.sdrNits);
    }

    if (settings.nightChanged) {
        output->setColorTemp(settings.nightOn ? settings.nightK : 0);
    }

    if (settings.themeChanged) {
        comp->theme.setSeeds(settings.neutral, settings.selection);
    }

    if (settings.layoutChanged && comp->wayland) {
        comp->wayland->setLayout(settings.layoutSel);
    }

    if (settings.pointerChanged && comp->input) {
        comp->input->setPointerSpeed(settings.pointerSpeed);
    }

    if (settings.changed()) {
        scene.needsFrame = true;
    }

    if (osdMs) {
        u64 now = nowMsec();

        if (now >= osdMs) {
            osdMs = 0;
        } else {
            float rem = (float)(osdMs - now) / 1000.f;
            float alpha = rem > 0.3f ? 1.f : rem / 0.3f;

            if (osdKind == 1 && comp->mixer) {
                drawOsd(scene.outW, uiScale, "volume"_sv, comp->mixer->volume(), comp->mixer->muted(), alpha);
            } else if (osdKind == 2) {
                drawOsd(scene.outW, uiScale, "brightness"_sv, output->brightness(), false, alpha);
            } else if (osdKind == 3 && output->colorState().hdr()) {
                drawOsd(scene.outW, uiScale, "hdr"_sv, (float)(output->colorState().sdrWhiteNits / output->colorState().displayPeakNits), false, alpha);
            }

            scene.needsFrame = true;
        }
    }

    {
        InspectorInfo info;

        renderer->inspectorInfo(info);
        drawInspector(*comp, info, inspectorToggle, &inspectorState);
        inspectorToggle = false;
    }

    if (notifier) {
        drawHistory(*comp, historyToggle, &historyState);
        historyToggle = false;
    }

    drawLogView(*comp, logToggle, &logState);
    logToggle = false;

    if (logState) {
        // new lines land between frames; keep the tail fresh while open
        scene.needsFrame = true;
    }

    if (pickShow) {
        static const char hx[] = "0123456789abcdef";
        char h[8] = {'#', hx[pickR >> 4], hx[pickR & 15], hx[pickG >> 4], hx[pickG & 15], hx[pickB >> 4], hx[pickB & 15], 0};

        ImGui::SetNextWindowPos(ImVec2((float)scene.outW / 2.f, (float)scene.outH / 2.f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("##pick", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
            float sz = ImGui::GetFontSize() * 2.4f;
            ImVec2 p = ImGui::GetCursorScreenPos();

            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(pickR, pickG, pickB, 255));
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(180, 180, 190, 255));
            ImGui::Dummy(ImVec2(sz, sz));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(h);

            if (ImGui::SmallButton("copy")) {
                ImGui::SetClipboardText(h);
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pickShow = false;
            }

            ImGui::EndGroup();
        }

        ImGui::End();
        scene.needsFrame = true;
    }

    // the focus truth resets right before the windows rebind it, so
    // everything drawn earlier in the frame (the chrome, the dock) reads
    // last frame's value instead of an always-empty mid-frame one
    scene.focusedToplevel.reset();

    // client frames are half-scene.outW
    const ImVec2 fullPad = ImGui::GetStyle().WindowPadding;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fullPad.x * 0.5f, fullPad.y * 0.5f));

    int i = 0;

    forEach<Toplevel>(scene.toplevels, [&](Toplevel& value) {
        Toplevel* t = &value;
        Surface* root = t->surface.get();

        if (!t->mapped || t->minimized || !root || !root->texture) {
            if (root) {
                markTreeUnhovered(*root);
            }

            return;
        }

        StringView title = sv(t->title);

        if (title.length() > 280) {
            title = title.prefix(280);
        }

        auto& label = sb();

        label << title;

        if (t->unresponsive) {
            label << " (not responding)"_sv;
        }

        label << "###toplevel"_sv << (u64)t->id;
        ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40.f + 30.f * i, viewport->WorkPos.y + 30.f + 30.f * i), ImGuiCond_FirstUseEver);
        i++;

        const ImGuiStyle& st = ImGui::GetStyle();
        float header = t->csd ? 0.f : ImGui::GetFrameHeight();
        float chromeW = st.WindowPadding.x * 2;
        float chromeH = st.WindowPadding.y * 2 + header;

        t->applyW = (float)root->geomW() + chromeW;
        t->applyH = (float)root->geomH() + chromeH;

        // how much the applied size steps this frame — drives both the left/top
        // position compensation below and the end-of-transaction detection
        float stepW = t->applyW - t->lastApplyW;
        float stepH = t->applyH - t->lastApplyH;

        t->lastApplyW = t->applyW;
        t->lastApplyH = t->applyH;

        if (t->maximized && !t->maximizedApplied) {
            t->restoreX = t->curX;
            t->restoreY = t->curY;
            t->restoreW = root->geomW();
            t->restoreH = root->geomH();
            t->maximizedApplied = true;
        } else if (!t->maximized && t->maximizedApplied) {
            t->maximizedApplied = false;
            t->restoreRequested = t->restoreW > 0 && t->restoreH > 0;
        }

        if (t->restoreRequested) {
            ImGui::SetNextWindowPos(ImVec2(t->restoreX, t->restoreY), ImGuiCond_Always);
        }

        // xdg-toplevel-drag: a torn-off window follows the cursor. Pin it to
        // (cursor - attach offset) and float it out of any dock node — this
        // is exactly the tab-tear gesture. Overrides the size-driven pos below
        bool dragTracking = (Toplevel*)t == scene.dragToplevel.get();

        if (dragTracking) {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2((float)posX - scene.dragToplevelOffX, (float)posY - scene.dragToplevelOffY), ImGuiCond_Always);
            t->docked = false;
        }

        if (!dragTracking && !t->docked && !t->fullscreen && !t->maximized) {
            // when the size steps to a client-committed buffer during a left/top
            // drag, move the top-left by the same delta so the opposite edge
            // stays put and the window grows toward the hand
            if (t->resizeAnchor & kResizeActive) {
                float nx = t->curX - ((t->resizeAnchor & kResizeLeft) ? stepW : 0.f);
                float ny = t->curY - ((t->resizeAnchor & kResizeTop) ? stepH : 0.f);

                if (nx != t->curX || ny != t->curY) {
                    ImGui::SetNextWindowPos(ImVec2(nx, ny), ImGuiCond_Always);
                }
            }

            // the window is a function of the client's committed geometry:
            // a border drag never resizes it directly (the constraint
            // callback pins it), it only asks — the size steps here, when
            // the geometry answers; initial map sizing and the return from
            // fullscreen are the same rule
            ImGui::SetNextWindowSize(ImVec2(t->applyW, t->applyH), ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, FLT_MAX), toplevelSizeCb, t);
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (t->maximized) {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
            flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
        }

        // csd clients (gtk) bring their own header bar; ours would double it.
        // without a title bar imgui exempts the window from
        // move-from-titlebar-only, so any in-content drag would move the
        // window instead of reaching the client — NoMove, lifted for the one
        // frame that serves a client-requested move (the move persists,
        // imgui does not re-check the flag mid-drag)
        if (t->csd) {
            flags |= ImGuiWindowFlags_NoTitleBar;

            if (!t->moveRequested) {
                flags |= ImGuiWindowFlags_NoMove;
            }
        }

        if (t->fullscreen) {
            // NoDocking alone does not pull a docked window out of its node
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)scene.outW, (float)scene.outH), ImGuiCond_Always);
            flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;

            // the content must cover the output edge to edge: window padding
            // would carve an 8px gutter and shrink the configured size
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        }

        // the ANR state must be visible before any interaction: a red title
        // bar plus the label suffix
        if (t->unresponsive) {
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.42f, 0.11f, 0.09f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.55f, 0.14f, 0.11f, 1.f));
        }

        bool stayOpen = true;

        if (ImGui::Begin(label.cStr(), t->csd ? nullptr : &stayOpen, flags)) {
            t->docked = ImGui::IsWindowDocked();
            t->tabHidden = false;

            // remember imgui's truth of the position, the base for next frame's
            // left/top resize compensation
            ImVec2 wp = ImGui::GetWindowPos();

            t->curX = wp.x;
            t->curY = wp.y;

            if (ImGui::IsWindowFocused()) {
                scene.focusedToplevel.bind(t->weak);

                // gaining focus promotes the window in the dock MRU order;
                // losing it changes nothing. Bump only when the latest
                // holder actually changes, not every frame — the holder is
                // the one whose stamp equals the counter (0 = nobody yet)
                if (t->focusedAt != scene.focusSeq || !scene.focusSeq) {
                    t->focusedAt = ++scene.focusSeq;
                    scene.needsFrame = true;
                }
            }

            if (t->raiseRequested) {
                t->raiseRequested = false;
                ImGui::SetWindowFocus();
            }

            if (t->moveRequested) {
                if (!t->fullscreen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImGuiWindow* window = ImGui::GetCurrentWindow();

                    ImGui::StartMouseMovingWindowOrNode(window, window->DockNode, true);
                }

                t->moveRequested = false;
            }

            if (t->resizeEdges) {
                if (!t->docked && !t->fullscreen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse = ImGui::GetMousePos();

                    t->clientResizeEdges = t->resizeEdges;
                    t->resizeStartMouseX = mouse.x;
                    t->resizeStartMouseY = mouse.y;
                    t->resizeStartW = t->applyW;
                    t->resizeStartH = t->applyH;
                    t->resizeAnchor = kResizeActive | ((t->resizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) ? kResizeLeft : 0) | ((t->resizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) ? kResizeTop : 0);
                }

                t->resizeEdges = 0;
            }

            if (t->clientResizeEdges) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse = ImGui::GetMousePos();
                    float dx = mouse.x - t->resizeStartMouseX;
                    float dy = mouse.y - t->resizeStartMouseY;
                    float rw = (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_RIGHT) ? dx : (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) ? -dx : 0.f;
                    float rh = (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM) ? dy : (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) ? -dy : 0.f;

                    t->dragW = t->resizeStartW + rw;
                    t->dragH = t->resizeStartH + rh;

                    if (t->dragW < chromeW + 1.f) {
                        t->dragW = chromeW + 1.f;
                    }

                    if (t->dragH < chromeH + 1.f) {
                        t->dragH = chromeH + 1.f;
                    }
                } else {
                    t->clientResizeEdges = 0;
                }
            }

            if (t->docked || t->fullscreen || t->maximized) {
                // the node/screen dictates the size, the client must fill it
                ImVec2 avail = ImGui::GetContentRegionAvail();

                t->desiredW = (int)avail.x;
                t->desiredH = (int)avail.y;
            } else if (t->restoreRequested) {
                t->desiredW = t->restoreW;
                t->desiredH = t->restoreH;

                if (root->geomW() == t->restoreW && root->geomH() == t->restoreH) {
                    t->restoreRequested = false;
                }
            } else if (t->dragW > 0.f) {
                // floating drag: the request, in client pixels
                t->desiredW = (int)(t->dragW - chromeW);
                t->desiredH = (int)(t->dragH - chromeH);
            } else {
                // steady state: ask for what the client already has, i.e.
                // nothing — floating configures originate from drags only
                t->desiredW = root->geomW();
                t->desiredH = root->geomH();

                // no active drag and the size has stopped changing: the resize
                // transaction is done, stop compensating so a move can proceed
                if ((t->resizeAnchor & kResizeActive) && stepW == 0.f && stepH == 0.f) {
                    t->resizeAnchor = 0;
                }
            }

            if (!t->docked && !t->fullscreen) {
                if (t->minW > 0 && t->desiredW < t->minW) {
                    t->desiredW = t->minW;
                }

                if (t->minH > 0 && t->desiredH < t->minH) {
                    t->desiredH = t->minH;
                }

                if (t->maxW > 0 && t->desiredW > t->maxW) {
                    t->desiredW = t->maxW;
                }

                if (t->maxH > 0 && t->desiredH > t->maxH) {
                    t->desiredH = t->maxH;
                }
            }

            t->viewGeomW = root->geomW();
            t->viewGeomH = root->geomH();
            t->dragW = 0.f;
            t->dragH = 0.f;

            ImVec2 origin = ImGui::GetCursorScreenPos();

            renderer->drawSurfaceTree(*root, origin.x, origin.y);

        } else {
            t->tabHidden = true;
            markTreeUnhovered(*root);
        }

        ImGui::End();

        if (t->fullscreen) {
            ImGui::PopStyleVar(2);
        }

        if (!stayOpen) {
            // an unresponsive window cannot answer xdg close: escalate to
            // the Terminate/Wait dialog instead of a dead-letter event
            if (t->unresponsive) {
                anrToggle = true;
                anrTarget.bind(t->weak);
            } else {
                t->closeRequested = true;
            }

            scene.needsFrame = true;
        }

        if (t->unresponsive) {
            ImGui::PopStyleColor(2);
        }
    });

    ImGui::PopStyleVar();

    forEach<Popup>(scene.popups, [&](Popup& p) {
        Surface* ps = p.surface.get();

        if (!p.mapped || !ps || !ps->texture || !p.parent) {
            if (ps) {
                markTreeUnhovered(*ps);
            }
        } else {
            renderer->drawSurfaceTreeOverlay(*ps, p.parent->imgX + (float)p.parent->geomX() + (float)p.x, p.parent->imgY + (float)p.parent->geomY() + (float)p.y);
        }
    });

    if (scene.dragIcon && scene.dragIcon->texture) {
        ImVec2 mp = ImGui::GetMousePos();

        renderer->drawSurfaceTreeOverlay(*scene.dragIcon.get(), mp.x + 4, mp.y + 4);
    }

    if (scene.imePopup && scene.imePopup->texture) {
        renderer->drawSurfaceTreeOverlay(*scene.imePopup.get(), scene.imePopupX, scene.imePopupY);
    }

    bool overClient = false;

    forEach<Surface, SceneNode>(scene.surfaces, [&](Surface& s) {
        // decoration surfaces (cursor image, drag icon) ride the pointer:
        // their hover flags go stale the moment they stop being drawn and
        // would pin this true forever; contentless surfaces likewise keep
        // the flag from their last drawn frame
        if (&s != scene.cursorSurface && &s != scene.dragIcon.get() && s.hovered && s.hasContent) {
            overClient = true;
        }
    });

    if (altTabActive && !altTabSel.get()) {
        // the selected window died under the overlay
        altTabActive = false;
    }

    if (launcherState || launcherToggle) {
        // the dialog is on screen this frame or flipping — one more frame
        // settles either way
        scene.needsFrame = true;
    }

    {
        Buffer cmd;
        LauncherAction act = LauncherAction::none;
        bool terminal = false;

        if (drawLauncher(*comp, launcherToggle, &launcherState, cmd, act, terminal, launcherX, launcherY)) {
            switch (act) {
                case LauncherAction::lockScreen:
                    openLockOverlay(*comp, &lockState);
                    break;
                case LauncherAction::settings:
                    settingsToggle = true;
                    break;
                case LauncherAction::notifications:
                    historyToggle = true;
                    break;
                case LauncherAction::inspector:
                    inspectorToggle = true;
                    break;
                case LauncherAction::colorPicker:
                    pickArmed = true;
                    break;
                case LauncherAction::logView:
                    logToggle = true;
                    break;
                case LauncherAction::none:
                    spawnClient(*comp, sv(cmd), scene.socketName, terminal);
                    break;
            }
        }

        launcherToggle = false;
    }

    // ui owns the pointer when it is over our widgets but not over client
    // content (client windows are imgui windows too, hence the second term)
    scene.ptrCaptured = ImGui::GetIO().WantCaptureMouse && !overClient;

    // everything below draws into the foreground list, which sits on top of
    // all windows anyway — and it MUST happen before ImGui::Render(): draw
    // data totals are snapshotted there, late commands are silently dropped
    if (altTabActive) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float th = 120.f * uiScale;
        float pad = 12.f * uiScale;
        float total = pad;
        int count = 0;

        forEach<Toplevel>(scene.toplevels, [&](Toplevel& value) {
            Toplevel* t = &value;

            if (!t->mapped || !t->surface || !t->surface->texture) {
                return;
            }

            float sw = (float)t->surface->geomW(), sh = (float)t->surface->geomH();
            float tw = sh > 0.f ? th * sw / sh : th;

            total += (tw > th * 2.f ? th * 2.f : tw) + pad;
            count++;
        });

        if (count) {
            float lineH = ImGui::GetFontSize();
            float boxH = th + lineH + pad * 3.f;
            float x = ((float)scene.outW - total) / 2.f;
            float y0 = ((float)scene.outH - boxH) / 2.f;

            dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + total, y0 + boxH), IM_COL32(18, 18, 24, 235), 8.f * uiScale);
            x += pad;

            forEach<Toplevel>(scene.toplevels, [&](Toplevel& value) {
                Toplevel* t = &value;

                if (!t->mapped || !t->surface || !t->surface->texture) {
                    return;
                }

                float sw = (float)t->surface->geomW(), sh = (float)t->surface->geomH();
                float tw = sh > 0.f ? th * sw / sh : th;

                if (tw > th * 2.f) {
                    tw = th * 2.f;
                }

                float y = y0 + pad;

                renderer->drawSurfaceRect(*t->surface, dl, x, y, x + tw, y + th);

                if (t == altTabSel.get()) {
                    dl->AddRect(ImVec2(x - 2.f, y - 2.f), ImVec2(x + tw + 2.f, y + th + 2.f), themeColorU32(comp->theme.accent), 0.f, 0, 3.f);
                }

                if (u64 tabIcon = comp->iconResolver->iconTexture(t->icon(*comp))) {
                    float isz = 20.f * uiScale;

                    dl->AddImage((ImTextureID)tabIcon, ImVec2(x + 3.f, y + 3.f), ImVec2(x + 3.f + isz, y + 3.f + isz));
                }

                StringView title = sv(t->title);

                if (title.length() > 24) {
                    title = title.prefix(24);
                }

                auto& lbl = sb();

                lbl << title;
                dl->AddText(ImVec2(x, y + th + pad * 0.75f), IM_COL32(230, 230, 230, 255), lbl.cStr());
                x += tw + pad;
            });
        }
    }

    drawLockOverlay(*comp, &lockState);

    if (lockState) {
        scene.kbCaptured = true;
        scene.ptrCaptured = true;
    }

    // xdg-system-bell: a brief screen flash on ring, fading over ~150ms
    if (scene.bellMs) {
        u64 now = nowMsec();
        u64 age = now >= scene.bellMs ? now - scene.bellMs : 0;

        if (age < 150) {
            float a = (1.f - (float)age / 150.f) * 0.35f;

            ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0.f, 0.f), ImVec2((float)scene.outW, (float)scene.outH), IM_COL32(255, 255, 255, (int)(a * 255.f)));
            scene.needsFrame = true;
        } else {
            scene.bellMs = 0;
        }
    }

    cursorUi(scene, overClient);

    // the scanout gate reads one scalar instead of the ui internals; the
    // bell flash counts too — a direct-scanout frame would hide it
    scene.overlayActive = launcherState || calendarState || wifiState || inspectorState || historyState || logState || anrState || settingsState || lockState || altTabActive || osdMs != 0 || pickArmed || pickShow || scene.bellMs != 0 || toastsActive();
}

void DesktopImpl::cursorUi(Scene& scene, bool overClient) {
    Surface* cs = overClient && scene.cursorSurface && scene.cursorSurface->texture ? scene.cursorSurface : nullptr;

    if (cs) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    } else if (overClient && scene.cursorShape != CursorKind::unset) {
        ImGuiMouseCursor c = ImGuiMouseCursor_Arrow;

        switch (scene.cursorShape) {
            case CursorKind::hidden:
                c = ImGuiMouseCursor_None;
                break;
            case CursorKind::text:
                c = ImGuiMouseCursor_TextInput;
                break;
            case CursorKind::hand:
                c = ImGuiMouseCursor_Hand;
                break;
            case CursorKind::grab:
                c = ImGuiMouseCursor_Hand;
                break;
            case CursorKind::move:
                c = ImGuiMouseCursor_ResizeAll;
                break;
            case CursorKind::nsResize:
                c = ImGuiMouseCursor_ResizeNS;
                break;
            case CursorKind::ewResize:
                c = ImGuiMouseCursor_ResizeEW;
                break;
            case CursorKind::neswResize:
                c = ImGuiMouseCursor_ResizeNESW;
                break;
            case CursorKind::nwseResize:
                c = ImGuiMouseCursor_ResizeNWSE;
                break;
            case CursorKind::notAllowed:
                c = ImGuiMouseCursor_NotAllowed;
                break;
            case CursorKind::wait:
                c = ImGuiMouseCursor_Wait;
                break;
            default:
                break;
        }

        ImGui::SetMouseCursor(c);
    }

    if (!scene.drawCursor) {
        return;
    }

    ImVec2 mp = ImGui::GetMousePos();
    int kind = ImGui::GetMouseCursor();

    if (renderer->cursorPlane(kind, cs, mp.x, mp.y, scene.cursorHotX, scene.cursorHotY)) {
        return;
    }

#ifdef IMWAY_FOR_TESTS
    if (getenv("IMWAY_DEBUG_CURSOR") && scene.framesDone % 120 == 0) {
        *(comp->log) << "cursor dbg: kind "_sv << kind << ", mp "_sv << mp.x << ","_sv << mp.y << ", overClient "_sv << (int)overClient << ", cs "_sv << (int)(cs != nullptr) << ", shape "_sv << (int)scene.cursorShape << endL;
    }
#endif

    if (cs) {
        renderer->drawSurfaceTreeOverlay(*cs, mp.x - scene.cursorHotX, mp.y - scene.cursorHotY);
    } else if (kind != ImGuiMouseCursor_None) {
        drawCursorShape(ImGui::GetForegroundDrawList(), mp, uiScale, kind);
    }
}

void DesktopImpl::build() {
    buildUi(*scene);
}

bool DesktopImpl::toastsActive() const {
    bool any = false;

    if (notifier) {
        notifier->active([&](Toast&) {
            any = true;
        });
    }

    return any;
}

void DesktopImpl::onListen(void*) {
    if (!placed) {
        // before any input arrives the cursor sits at the screen center
        placed = true;
        posX = scene->outW / 2.0;
        posY = scene->outH / 2.0;
    } else {
        clampPos();
    }

    ImGui::GetIO().AddMousePosEvent((float)posX, (float)posY);
}

DesktopImpl::DesktopImpl(Composer& c, float scale)
    : comp(&c)
    , scene(c.scene)
    , output(c.output)
    , renderer(c.renderer)
    , keyboard(c.kb)
    , notifier(c.notifier)
    , uiScale(scale)
    , nextUiScale(scale)
{
    scene->uiScale = scale;
    c.inputSinks.pushFront((InputSink*)this);
    c.mixerListeners.pushBack(c.pool->make<CallDesktopVolume>(this));
    c.wifiListeners.pushBack(c.pool->make<CallDesktopWifi>(this));
    c.outputResizedListeners.pushBack((Listener*)this);

    for (const ChordDef& d : kChords) {
        bindingsView.pushBack({d.chord, d.action});
    }

    bindingsView.pushBack({"Alt release"_sv, "commit the window switch"_sv});
    bindingsView.pushBack({"Esc"_sv, "cancel the window switch"_sv});
    bindingsView.pushBack({"XF86 volume keys"_sv, "volume up/down 5%, mute"_sv});
    bindingsView.pushBack({"XF86 brightness keys"_sv, "backlight 5% or sdr white 10 nits"_sv});
}

DesktopImpl::~DesktopImpl() noexcept {
    // the dialogs hold wl-side popup surfaces; close them while the imgui
    // context (owned by the renderer, which outlives us in the pool) is up
    dialog(settingsState);
    dialog(anrState);
    dialog(calendarState);
    dialog(wifiState);
    dialog(inspectorState);
    dialog(historyState);
    dialog(launcherState);
    closeLockOverlay(&lockState);
}

Desktop* Desktop::create(Composer& c, float uiScale) {
    return c.pool->make<DesktopImpl>(c, uiScale);
}
