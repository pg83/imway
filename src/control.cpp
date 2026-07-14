// Control-канал: FIFO с текстовыми командами для инъекции input и управления.
// Команды: motion X Y | button left|right|middle press|release |
//          key CODE press|release | type TEXT | scroll N |
//          screenshot PATH | quit

#include "kms.h"
#include "renderer.h"
#include "seat.h"
#include "server.h"
#include "util.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    // ascii → (evdev code, needs shift) для us-раскладки
    bool asciiToKey(char c, u32& code, bool& shift) {
        static const struct {
            char ch;
            u32 code;
            bool shift;
        } table[] = {
            {'a', KEY_A, 0}, {'b', KEY_B, 0}, {'c', KEY_C, 0}, {'d', KEY_D, 0}, {'e', KEY_E, 0},
            {'f', KEY_F, 0}, {'g', KEY_G, 0}, {'h', KEY_H, 0}, {'i', KEY_I, 0}, {'j', KEY_J, 0},
            {'k', KEY_K, 0}, {'l', KEY_L, 0}, {'m', KEY_M, 0}, {'n', KEY_N, 0}, {'o', KEY_O, 0},
            {'p', KEY_P, 0}, {'q', KEY_Q, 0}, {'r', KEY_R, 0}, {'s', KEY_S, 0}, {'t', KEY_T, 0},
            {'u', KEY_U, 0}, {'v', KEY_V, 0}, {'w', KEY_W, 0}, {'x', KEY_X, 0}, {'y', KEY_Y, 0},
            {'z', KEY_Z, 0}, {'1', KEY_1, 0}, {'2', KEY_2, 0}, {'3', KEY_3, 0}, {'4', KEY_4, 0},
            {'5', KEY_5, 0}, {'6', KEY_6, 0}, {'7', KEY_7, 0}, {'8', KEY_8, 0}, {'9', KEY_9, 0},
            {'0', KEY_0, 0}, {' ', KEY_SPACE, 0}, {'-', KEY_MINUS, 0}, {'=', KEY_EQUAL, 0},
            {'/', KEY_SLASH, 0}, {'.', KEY_DOT, 0}, {',', KEY_COMMA, 0}, {';', KEY_SEMICOLON, 0},
            {'\'', KEY_APOSTROPHE, 0}, {'[', KEY_LEFTBRACE, 0}, {']', KEY_RIGHTBRACE, 0},
            {'\\', KEY_BACKSLASH, 0}, {'`', KEY_GRAVE, 0}, {'\t', KEY_TAB, 0},
            {'_', KEY_MINUS, 1}, {'+', KEY_EQUAL, 1}, {'?', KEY_SLASH, 1}, {'>', KEY_DOT, 1},
            {'<', KEY_COMMA, 1}, {':', KEY_SEMICOLON, 1}, {'"', KEY_APOSTROPHE, 1},
            {'{', KEY_LEFTBRACE, 1}, {'}', KEY_RIGHTBRACE, 1}, {'|', KEY_BACKSLASH, 1},
            {'~', KEY_GRAVE, 1}, {'!', KEY_1, 1}, {'@', KEY_2, 1}, {'#', KEY_3, 1},
            {'$', KEY_4, 1}, {'%', KEY_5, 1}, {'^', KEY_6, 1}, {'&', KEY_7, 1}, {'*', KEY_8, 1},
            {'(', KEY_9, 1}, {')', KEY_0, 1},
        };

        if (c >= 'A' && c <= 'Z') {
            c = (char)tolower(c);
            shift = true;
        } else {
            shift = false;
        }

        for (auto& e : table) {
            if (e.ch == c) {
                code = e.code;
                shift = shift || e.shift;

                return true;
            }
        }

        return false;
    }

    void controlIoCb(struct ev_loop*, ev_io* w, int);

    struct ControlImpl: public Control {
        Server* server = nullptr;
        int fd = -1;
        ev_io io{};
        char path[256] = "";
        char line[1024] = "";
        size_t lineLen = 0;

        ControlImpl(Server& srv, const char* fifoPath);
        ~ControlImpl() noexcept override;

        void handleLine(const char* cmd);
        void handleInput();
        void reopen();
    };

    void controlIoCb(struct ev_loop*, ev_io* w, int) {
        ((ControlImpl*)w->data)->handleInput();
    }
}

ControlImpl::ControlImpl(Server& srv, const char* fifoPath)
    : server(&srv)
{
    STD_VERIFY(strlen(fifoPath) < sizeof(path));
    strcpy(path, fifoPath);
    unlink(path);
    STD_VERIFY(mkfifo(path, 0600) == 0);

    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    STD_VERIFY(fd >= 0);

    ev_io_init(&io, controlIoCb, fd, EV_READ);
    io.data = this;
    ev_io_start(server->loop, &io);
    sysO << "imway: control FIFO: "_sv << (const char*)path << endL;
}

ControlImpl::~ControlImpl() noexcept {
    if (fd >= 0) {
        ev_io_stop(server->loop, &io);
        close(fd);
    }

    if (path[0]) {
        unlink(path);
    }
}

void ControlImpl::handleLine(const char* cmd) {
    Seat& seat = *server->seat;
    char a[64] = {0}, b[64] = {0};
    double x, y;
    u32 code;

    if (sscanf(cmd, "motion %lf %lf", &x, &y) == 2) {
        seat.handleMotion(x, y);
    } else if (sscanf(cmd, "button %63s %63s", a, b) == 2) {
        u32 btn = !strcmp(a, "left") ? BTN_LEFT : !strcmp(a, "right") ? BTN_RIGHT : BTN_MIDDLE;

        seat.handleButton(btn, !strcmp(b, "press"));
    } else if (sscanf(cmd, "key %u %63s", &code, b) == 2) {
        seat.handleKey(code, !strcmp(b, "press"));
    } else if (!strncmp(cmd, "type ", 5)) {
        for (const char* p = cmd + 5; *p; p++) {
            u32 kc;
            bool shift;

            if (!asciiToKey(*p, kc, shift)) {
                continue;
            }

            if (shift) {
                seat.handleKey(KEY_LEFTSHIFT, true);
            }

            seat.handleKey(kc, true);
            seat.handleKey(kc, false);

            if (shift) {
                seat.handleKey(KEY_LEFTSHIFT, false);
            }
        }
    } else if (sscanf(cmd, "scroll %lf", &y) == 1) {
        seat.handleScroll(y);
    } else if (sscanf(cmd, "screenshot %63s", a) == 1) {
        // скриншот содержимого последнего отрендеренного кадра
        server->renderer->screenshot(a);
        sysO << "imway: screenshot by command: "_sv << (const char*)a << endL;
    } else if (!strcmp(cmd, "quit")) {
        ev_break(server->loop, EVBREAK_ALL);
    } else {
        sysE << "imway: unknown command: "_sv << cmd << endL;
    }
}

void ControlImpl::handleInput() {
    char tmp[512];

    for (;;) {
        ssize_t n = read(fd, tmp, sizeof tmp);

        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (tmp[i] == '\n') {
                    line[lineLen] = 0;
                    lineLen = 0;

                    if (line[0]) {
                        handleLine(line);
                    }
                } else if (lineLen + 1 < sizeof(line)) {
                    line[lineLen++] = tmp[i];
                }
            }
        } else if (n == 0) {
            reopen(); // писатель закрыл FIFO

            return;
        } else {
            return; // EAGAIN
        }
    }
}

void ControlImpl::reopen() {
    ev_io_stop(server->loop, &io);
    close(fd);

    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0) {
        return;
    }

    ev_io_init(&io, controlIoCb, fd, EV_READ);
    io.data = this;
    ev_io_start(server->loop, &io);
}

Control::~Control() noexcept {
}

Control* Control::create(ObjPool* pool, Server& server, const char* fifoPath) {
    return pool->make<ControlImpl>(server, fifoPath);
}
