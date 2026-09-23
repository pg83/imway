// A stand-in seatd for the libseat session. The runners have no seatd a
// scenario may drive (the real one owns a VT and switches it for real);
// this one speaks just enough of the seatd protocol (open/close seat,
// device opens refused, disable acknowledgement, ping) to play the seat
// manager's side of a VT switch. libseat 0.9 waits for the manager to
// acknowledge a disable, 0.8 neither waits nor understands the
// acknowledgement (one would sit unread in front of every later event),
// so the scenario, which knows the compositor's libseat, sends it:
//   serve              enable the seat as soon as it is opened
//   serve-inactive     open the seat, never enable it and garble the
//                      conversation
// It is started by imway-pre before the compositor boots, listens on
// seatd.sock in its working directory, serves one connection and exits
// with it, and is driven through the FIFO seatd-ctl there:
//   disable   ask the compositor to give the seat up (a switch away)
//   enable    hand the seat back (a switch back)
//   ack       acknowledge the compositor's disable (libseat 0.9)
//   hangup    drop the connection, as a dying seatd does
//   crash     ask for the seat and die with the compositor's answer
//             unread, as a seatd crashing mid-switch does: the
//             compositor's end reads a reset connection
// What the compositor sends lands in seatd-events: "open-seat",
// "disable-request" once it lets the seat go, "close-seat", "open PATH".

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define SERVER_EVENT(opcode) ((opcode) + (1 << 15))

#define CLIENT_OPEN_SEAT 1
#define CLIENT_CLOSE_SEAT 2
#define CLIENT_OPEN_DEVICE 3
#define CLIENT_CLOSE_DEVICE 4
#define CLIENT_DISABLE_SEAT 5
#define CLIENT_PING 7

#define SERVER_SEAT_OPENED SERVER_EVENT(1)
#define SERVER_SEAT_CLOSED SERVER_EVENT(2)
#define SERVER_DEVICE_CLOSED SERVER_EVENT(4)
#define SERVER_DISABLE_SEAT SERVER_EVENT(5)
#define SERVER_ENABLE_SEAT SERVER_EVENT(6)
#define SERVER_PONG SERVER_EVENT(7)
#define SERVER_SEAT_DISABLED SERVER_EVENT(9)
#define SERVER_ERROR SERVER_EVENT(0x7FFF)

struct header {
    uint16_t opcode;
    uint16_t size;
};

static int client = -1;
static int inactive;

static void event(const char* line) {
    FILE* f = fopen("seatd-events", "a");

    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

static int send_all(const void* p, size_t n) {
    const char* c = p;

    while (n) {
        ssize_t w = write(client, c, n);

        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        c += w;
        n -= (size_t)w;
    }

    return 0;
}

static int read_all(void* p, size_t n) {
    char* c = p;

    while (n) {
        ssize_t r = read(client, c, n);

        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        c += r;
        n -= (size_t)r;
    }

    return 0;
}

static void send_msg(uint16_t opcode, const void* body, uint16_t size) {
    struct header h = {opcode, size};

    send_all(&h, sizeof(h));

    if (size) {
        send_all(body, size);
    }
}

// the seat has one session: once it is over there is nothing to serve
static void hangup(void) {
    close(client);
    event("hangup");
    exit(0);
}

// one request from the compositor; -1 once it is gone
static int client_msg(void) {
    struct header h;
    char body[512];
    char line[600];

    if (read_all(&h, sizeof(h)) < 0 || h.size > sizeof(body) || (h.size && read_all(body, h.size) < 0)) {
        return -1;
    }

    switch (h.opcode) {
        case CLIENT_OPEN_SEAT: {
            char reply[2 + 6] = {6, 0, 's', 'e', 'a', 't', '0', 0};

            event("open-seat");
            send_msg(SERVER_SEAT_OPENED, reply, sizeof(reply));

            // a hangup here would race the reply: libseat may see the
            // hangup before it reads the seat's name. A request never made
            // is answered after it instead, which libseat cannot follow.
            if (inactive) {
                send_msg(SERVER_DEVICE_CLOSED, NULL, 0);
            } else {
                send_msg(SERVER_ENABLE_SEAT, NULL, 0);
            }
            break;
        }
        case CLIENT_CLOSE_SEAT:
            event("close-seat");
            send_msg(SERVER_SEAT_CLOSED, NULL, 0);
            break;
        case CLIENT_OPEN_DEVICE: {
            int err = ENODEV;

            body[h.size < sizeof(body) ? h.size : sizeof(body) - 1] = 0;
            snprintf(line, sizeof(line), "open %s", body + 2);
            event(line);
            send_msg(SERVER_ERROR, &err, sizeof(err));
            break;
        }
        case CLIENT_CLOSE_DEVICE:
            send_msg(SERVER_DEVICE_CLOSED, NULL, 0);
            break;
        case CLIENT_DISABLE_SEAT:
            event("disable-request");
            break;
        case CLIENT_PING:
            send_msg(SERVER_PONG, NULL, 0);
            break;
        default:
            snprintf(line, sizeof(line), "unexpected %u", (unsigned)h.opcode);
            event(line);
            break;
    }

    return 0;
}

static void command(const char* line) {
    if (client < 0) {
        return;
    }

    if (!strncmp(line, "disable", 7)) {
        send_msg(SERVER_DISABLE_SEAT, NULL, 0);
    } else if (!strncmp(line, "enable", 6)) {
        send_msg(SERVER_ENABLE_SEAT, NULL, 0);
    } else if (!strncmp(line, "ack", 3)) {
        send_msg(SERVER_SEAT_DISABLED, NULL, 0);
    } else if (!strncmp(line, "hangup", 6)) {
        hangup();
    } else if (!strncmp(line, "crash", 5)) {
        struct pollfd answer = {.fd = client, .events = POLLIN};

        send_msg(SERVER_DISABLE_SEAT, NULL, 0);
        poll(&answer, 1, 10000);
        hangup();
    }
}

int main(int argc, char** argv) {
    if (argc < 2 || (strcmp(argv[1], "serve") && strcmp(argv[1], "serve-inactive"))) {
        fprintf(stderr, "usage: %s serve|serve-inactive\n", argv[0]);
        return 2;
    }

    inactive = !strcmp(argv[1], "serve-inactive");
    // a compositor that never connects does not leave it behind forever
    alarm(120);

    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    snprintf(sa.sun_path, sizeof(sa.sun_path), "seatd.sock");
    unlink(sa.sun_path);

    if (s < 0 || bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0 || listen(s, 4) < 0) {
        perror("seatd.sock");
        return 1;
    }

    if (mkfifo("seatd-ctl", 0600) < 0 && errno != EEXIST) {
        perror("seatd-ctl");
        return 1;
    }

    // held open for writing too, so the scenario's writers come and go
    // without an end of file
    int fifo = open("seatd-ctl", O_RDWR | O_CLOEXEC);
    char pending[256];
    size_t used = 0;

    event("ready");

    for (;;) {
        struct pollfd p[3] = {
            {.fd = client, .events = POLLIN},
            {.fd = fifo, .events = POLLIN},
            {.fd = s, .events = POLLIN},
        };

        if (poll(p, 3, -1) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }

        if (client >= 0 && (p[0].revents & (POLLIN | POLLHUP))) {
            if (client_msg() < 0) {
                close(client);
                event("gone");
                return 0;
            }
        }

        if (p[1].revents & POLLIN) {
            ssize_t r = read(fifo, pending + used, sizeof(pending) - 1 - used);

            if (r > 0) {
                char* nl;

                used += (size_t)r;
                pending[used] = 0;

                while ((nl = strchr(pending, '\n'))) {
                    *nl = 0;
                    command(pending);
                    used -= (size_t)(nl + 1 - pending);
                    memmove(pending, nl + 1, used + 1);
                }
            }
        }

        if (p[2].revents & POLLIN) {
            int c = accept4(s, NULL, NULL, SOCK_CLOEXEC);

            if (c >= 0 && client < 0) {
                client = c;
            } else if (c >= 0) {
                close(c);
            }
        }
    }
}
