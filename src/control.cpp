// Control-канал: FIFO с текстовыми командами для инъекции input и управления.
// Команды: motion X Y | button left|right|middle press|release |
//          key CODE press|release | type TEXT | scroll N |
//          screenshot PATH | quit

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "renderer.hpp"
#include "seat.hpp"
#include "server.hpp"

struct Control {
    Server* server = nullptr;
    int fd = -1;
    ev_io io{};
    std::string buf;
    std::string path;

    bool init(Server&, const char* fifo_path);
    void finish();
    void handle_line(const std::string&);
    void reopen();
};

namespace {

// ascii → (evdev code, needs shift) для us-раскладки
bool ascii_to_key(char c, uint32_t& code, bool& shift) {
    static const struct { char ch; uint32_t code; bool shift; } table[] = {
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
        {'~', KEY_GRAVE, 1}, {'!', KEY_1, 1}, {'@', KEY_2, 1}, {'#', KEY_3, 1}, {'$', KEY_4, 1},
        {'%', KEY_5, 1}, {'^', KEY_6, 1}, {'&', KEY_7, 1}, {'*', KEY_8, 1}, {'(', KEY_9, 1},
        {')', KEY_0, 1},
    };
    if (c >= 'A' && c <= 'Z') c = (char)tolower(c), shift = true;
    else shift = false;
    for (auto& e : table)
        if (e.ch == c) {
            code = e.code;
            shift = shift || e.shift;
            return true;
        }
    return false;
}

void control_io_cb(struct ev_loop*, ev_io* w, int) {
    auto* c = (Control*)w->data;
    char tmp[512];
    for (;;) {
        ssize_t n = read(c->fd, tmp, sizeof tmp);
        if (n > 0) {
            c->buf.append(tmp, (size_t)n);
            size_t pos;
            while ((pos = c->buf.find('\n')) != std::string::npos) {
                std::string line = c->buf.substr(0, pos);
                c->buf.erase(0, pos + 1);
                if (!line.empty()) c->handle_line(line);
            }
        } else if (n == 0) {
            c->reopen(); // писатель закрыл FIFO
            return;
        } else {
            return; // EAGAIN
        }
    }
}

} // namespace

void Control::handle_line(const std::string& line) {
    Seat& seat = *server->seat;
    char a[64] = {0}, b[64] = {0};
    double x, y;
    uint32_t code;

    if (sscanf(line.c_str(), "motion %lf %lf", &x, &y) == 2) {
        seat.handle_motion(x, y);
    } else if (sscanf(line.c_str(), "button %63s %63s", a, b) == 2) {
        uint32_t btn = !strcmp(a, "left") ? BTN_LEFT : !strcmp(a, "right") ? BTN_RIGHT : BTN_MIDDLE;
        seat.handle_button(btn, !strcmp(b, "press"));
    } else if (sscanf(line.c_str(), "key %u %63s", &code, b) == 2) {
        seat.handle_key(code, !strcmp(b, "press"));
    } else if (line.rfind("type ", 0) == 0) {
        for (char ch : line.substr(5)) {
            uint32_t kc;
            bool shift;
            if (!ascii_to_key(ch, kc, shift)) continue;
            if (shift) seat.handle_key(KEY_LEFTSHIFT, true);
            seat.handle_key(kc, true);
            seat.handle_key(kc, false);
            if (shift) seat.handle_key(KEY_LEFTSHIFT, false);
        }
    } else if (sscanf(line.c_str(), "scroll %lf", &y) == 1) {
        seat.handle_scroll(y);
    } else if (sscanf(line.c_str(), "screenshot %63s", a) == 1) {
        // скриншот содержимого последнего отрендеренного кадра
        server->renderer->screenshot(a);
        std::printf("imway: скриншот по команде: %s\n", a);
    } else if (line == "quit") {
        ev_break(server->loop, EVBREAK_ALL);
    } else {
        std::fprintf(stderr, "imway: непонятная команда: %s\n", line.c_str());
    }
}

void Control::reopen() {
    ev_io_stop(server->loop, &io);
    close(fd);
    fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    ev_io_init(&io, control_io_cb, fd, EV_READ);
    io.data = this;
    ev_io_start(server->loop, &io);
}

bool Control::init(Server& srv, const char* fifo_path) {
    server = &srv;
    path = fifo_path;
    unlink(fifo_path);
    if (mkfifo(fifo_path, 0600) != 0) {
        std::perror("mkfifo");
        return false;
    }
    fd = open(fifo_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::perror("open fifo");
        return false;
    }
    ev_io_init(&io, control_io_cb, fd, EV_READ);
    io.data = this;
    ev_io_start(server->loop, &io);
    std::printf("imway: control FIFO: %s\n", fifo_path);
    return true;
}

void Control::finish() {
    if (fd >= 0) {
        ev_io_stop(server->loop, &io);
        close(fd);
    }
    if (!path.empty()) unlink(path.c_str());
}

Control* control_create(Server& server, const char* path) {
    auto* c = new Control();
    if (!c->init(server, path)) {
        delete c;
        return nullptr;
    }
    return c;
}

void control_destroy(Control* c) {
    if (!c) return;
    c->finish();
    delete c;
}
