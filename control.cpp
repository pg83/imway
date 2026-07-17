#include "composer.h"
#include "control.h"
#include "input_sink.h"
#include "renderer.h"
#include "util.h"

#include <ev.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
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
            c = (char)(c - 'A' + 'a');
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
        struct ev_loop* loop = nullptr;
        InputSink* sink = nullptr;
        Renderer* renderer = nullptr;
        int fd = -1;
        ev_io io{};
        StringBuilder path;
        char line[1024] = "";
        size_t lineLen = 0;

        ControlImpl(Composer& c, StringView fifoPath);
        ~ControlImpl() noexcept;

        void handleLine(StringView cmd);
        void handleInput();
        void reopen();
    };

    void controlIoCb(struct ev_loop*, ev_io* w, int) {
        ((ControlImpl*)w->data)->handleInput();
    }
}

ControlImpl::ControlImpl(Composer& c, StringView fifoPath)
    : loop(c.loop)
    , sink(c.renderer->sink())
    , renderer(c.renderer)
{
    path << fifoPath;
    unlink(path.cStr());
    STD_VERIFY(mkfifo(path.cStr(), 0600) == 0);

    fd = open(path.cStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    STD_VERIFY(fd >= 0);

    ev_io_init(&io, controlIoCb, fd, EV_READ);
    io.data = this;
    ev_io_start(loop, &io);
    sysO << "imway: control FIFO: "_sv << sv(path) << endL;
}

ControlImpl::~ControlImpl() noexcept {
    if (fd >= 0) {
        ev_io_stop(loop, &io);
        close(fd);
    }

    if (!sv(path).empty()) {
        unlink(path.cStr());
    }
}

void ControlImpl::handleLine(StringView cmd) {
    StringView verb, args;

    if (!cmd.split(' ', verb, args)) {
        verb = cmd;
        args = {};
    }

    if (verb == "motion"_sv) {
        StringView xs, ys;

        if (args.split(' ', xs, ys)) {
            sink->motion(parseFloat(xs), parseFloat(ys));
        }
    } else if (verb == "button"_sv) {
        StringView which, state;

        if (args.split(' ', which, state)) {
            u32 btn = which == "left"_sv ? BTN_LEFT : which == "right"_sv ? BTN_RIGHT : BTN_MIDDLE;

            sink->button(btn, state == "press"_sv);
        }
    } else if (verb == "key"_sv) {
        StringView code, state;

        if (args.split(' ', code, state)) {
            sink->key((u32)code.stou(), state == "press"_sv);
        }
    } else if (verb == "type"_sv) {
        for (u8 c : args) {
            u32 kc;
            bool shift;

            if (!asciiToKey((char)c, kc, shift)) {
                continue;
            }

            if (shift) {
                sink->key(KEY_LEFTSHIFT, true);
            }

            sink->key(kc, true);
            sink->key(kc, false);

            if (shift) {
                sink->key(KEY_LEFTSHIFT, false);
            }
        }
    } else if (verb == "hscroll"_sv) {
        ScrollEvent ev;

        ev.dx = parseFloat(args);
        ev.discreteX = (i32)ev.dx;
        ev.source = ScrollSource::wheel;
        sink->scroll(ev);
    } else if (verb == "scroll"_sv) {
        ScrollEvent ev;

        ev.dy = parseFloat(args);
        ev.discreteY = (i32)ev.dy;
        ev.source = ScrollSource::wheel;
        sink->scroll(ev);
    } else if (verb == "screenshot"_sv) {
        renderer->screenshot(args);
        sysO << "imway: screenshot by command: "_sv << args << endL;
    } else if (verb == "quit"_sv) {
        ev_break(loop, EVBREAK_ALL);
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
                    if (lineLen) {
                        handleLine({(const u8*)line, lineLen});
                    }

                    lineLen = 0;
                } else if (lineLen + 1 < sizeof(line)) {
                    line[lineLen++] = tmp[i];
                }
            }
        } else if (n == 0) {
            reopen();

            return;
        } else {
            return;
        }
    }
}

void ControlImpl::reopen() {
    ev_io_stop(loop, &io);
    close(fd);

    fd = open(path.cStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0) {
        return;
    }

    ev_io_init(&io, controlIoCb, fd, EV_READ);
    io.data = this;
    ev_io_start(loop, &io);
}

Control* Control::create(Composer& c, StringView fifoPath) {
    return c.pool->make<ControlImpl>(c, fifoPath);
}
