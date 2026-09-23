// A stand-in sndiod for the sndio mixer's control handling. The real
// sndiod on the ALSA null device offers output.level but no output.mute,
// never takes a control away and has no groups the mixer must skip; this
// one speaks just enough of the aucat protocol (AUTH, HELLO, CTLSUB,
// CTLSET, BYE) to serve a hardware-like control set:
//   5  output.level  NUM 0..127
//   6  output.mute   SW
//   7  app/output.level      another group: not the server's
//   8  input.level           another node
//   9  output.balance NUM    another function
//  10  output.dither  SW     another switch
// It is started by imway-pre ("serve") before the compositor boots and
// driven through the FIFO sndio-ctl in its working directory:
//   val A V   report control A changed to V from outside
//   del A     take control A away
//   add A     offer control A (again), at its last value
//   ping      append "pong" to the event log once everything the
//             compositor sent before it has been read
// Every value the compositor writes lands in sndio-events as "set A V", and
// is echoed back to it as sndiod does to every client.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
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

#define AMSG_ACK 0
#define AMSG_DATA 5
#define AMSG_HELLO 10
#define AMSG_BYE 11
#define AMSG_AUTH 12
#define AMSG_CTLSUB_OLD 13
#define AMSG_CTLSET 14
#define AMSG_CTLSYNC 15
#define AMSG_CTLSUB 16

#define CTL_NONE 0
#define CTL_NUM 2
#define CTL_SW 3

struct amsg {
    uint32_t cmd;
    uint32_t pad;
    union {
        uint8_t raw[32];
        struct {
            uint32_t size;
        } data;
        struct {
            uint8_t desc, val;
        } ctlsub;
        struct {
            uint16_t addr, val;
        } ctlset;
    } u;
};

struct node {
    char name[16];
    int16_t unit;
    uint8_t pad[2];
};

struct desc {
    struct node node0;
    struct node node1;
    char func[16];
    char group[16];
    uint8_t type;
    uint8_t pad1;
    uint16_t addr;
    uint16_t maxval;
    uint16_t curval;
    uint32_t pad2[4];
    char display[32];
};

// a client asking with CTLSUB_OLD reads descriptions without "display"
#define OLD_DESC_SIZE 92

struct ctl {
    const char* group;
    const char* node;
    const char* func;
    int type;
    int addr;
    int maxval;
    int val;
};

static struct ctl ctls[] = {
    {"", "output", "level", CTL_NUM, 5, 127, 100},
    {"", "output", "mute", CTL_SW, 6, 1, 0},
    {"app", "output", "level", CTL_NUM, 7, 127, 127},
    {"", "input", "level", CTL_NUM, 8, 127, 64},
    {"", "output", "balance", CTL_NUM, 9, 127, 64},
    {"", "output", "dither", CTL_SW, 10, 1, 0},
};

#define NCTLS (sizeof(ctls) / sizeof(ctls[0]))

static int client = -1;
static size_t desc_size;
static int subscribed;

static void event(const char* line) {
    FILE* f = fopen("sndio-events", "a");

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

static int read_all(int fd, void* p, size_t n) {
    char* c = p;

    while (n) {
        ssize_t r = read(fd, c, n);

        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        c += r;
        n -= (size_t)r;
    }

    return 0;
}

static void send_msg(uint32_t cmd, uint16_t addr, uint16_t val) {
    struct amsg m;

    memset(&m, 0xff, sizeof(m));
    m.cmd = htonl(cmd);

    if (cmd == AMSG_CTLSET) {
        m.u.ctlset.addr = htons(addr);
        m.u.ctlset.val = htons(val);
    }

    send_all(&m, sizeof(m));
}

// the given controls' descriptions, a deleted one typed CTL_NONE, then the
// sync that ends the batch
static void send_descs(struct ctl** list, size_t count, int deleted) {
    char buf[sizeof(struct desc) * NCTLS];
    struct amsg m;

    memset(buf, 0, sizeof(buf));

    for (size_t i = 0; i < count; i++) {
        struct desc* d = (struct desc*)(buf + i * desc_size);

        snprintf(d->group, sizeof(d->group), "%s", list[i]->group);
        snprintf(d->node0.name, sizeof(d->node0.name), "%s", list[i]->node);
        d->node0.unit = htons((uint16_t)-1);
        d->node1.unit = htons((uint16_t)-1);
        snprintf(d->func, sizeof(d->func), "%s", list[i]->func);
        d->type = (uint8_t)(deleted ? CTL_NONE : list[i]->type);
        d->addr = htons((uint16_t)list[i]->addr);
        d->maxval = htons((uint16_t)list[i]->maxval);
        d->curval = htons((uint16_t)list[i]->val);
    }

    memset(&m, 0xff, sizeof(m));
    m.cmd = htonl(AMSG_DATA);
    m.u.data.size = htonl((uint32_t)(count * desc_size));
    send_all(&m, sizeof(m));
    send_all(buf, count * desc_size);
    send_msg(AMSG_CTLSYNC, 0, 0);
}

static struct ctl* find(int addr) {
    for (size_t i = 0; i < NCTLS; i++) {
        if (ctls[i].addr == addr) return &ctls[i];
    }

    return NULL;
}

// one message from the compositor; -1 once it is gone
static int client_msg(void) {
    struct amsg m;
    char line[64];

    if (read_all(client, &m, sizeof(m)) < 0) return -1;

    switch (ntohl(m.cmd)) {
        case AMSG_AUTH:
            break;
        case AMSG_HELLO:
            send_msg(AMSG_ACK, 0, 0);
            break;
        case AMSG_CTLSUB_OLD:
        case AMSG_CTLSUB:
            if (m.u.ctlsub.desc && !subscribed) {
                struct ctl* all[NCTLS];

                for (size_t i = 0; i < NCTLS; i++) all[i] = &ctls[i];
                subscribed = 1;
                desc_size = ntohl(m.cmd) == AMSG_CTLSUB ? sizeof(struct desc) : OLD_DESC_SIZE;
                send_descs(all, NCTLS, 0);
            }
            break;
        case AMSG_CTLSET: {
            int addr = ntohs(m.u.ctlset.addr), val = ntohs(m.u.ctlset.val);
            struct ctl* c = find(addr);

            snprintf(line, sizeof(line), "set %d %d", addr, val);
            event(line);

            if (c) {
                c->val = val;
                send_msg(AMSG_CTLSET, (uint16_t)addr, (uint16_t)val);
            }
            break;
        }
        case AMSG_BYE:
            return -1;
        default:
            snprintf(line, sizeof(line), "unexpected %u", ntohl(m.cmd));
            event(line);
            break;
    }

    return 0;
}

static void command(char* line) {
    int addr = 0, val = 0;

    if (sscanf(line, "val %d %d", &addr, &val) == 2) {
        struct ctl* c = find(addr);

        if (c) c->val = val;
        send_msg(AMSG_CTLSET, (uint16_t)addr, (uint16_t)val);
    } else if (sscanf(line, "del %d", &addr) == 1) {
        struct ctl* c = find(addr);

        if (c) send_descs(&c, 1, 1);
    } else if (sscanf(line, "add %d", &addr) == 1) {
        struct ctl* c = find(addr);

        if (c) send_descs(&c, 1, 0);
    } else if (!strncmp(line, "ping", 4)) {
        event("pong");
    }
}

// -1 where the parent directory does not exist on this host
static int listen_on(const char* dir) {
    struct sockaddr_un sa = {.sun_family = AF_UNIX};

    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        return -1;
    }

    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s/sock0", dir);
    unlink(sa.sun_path);

    if (s < 0 || bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0 || listen(s, 4) < 0) {
        perror(sa.sun_path);
        exit(1);
    }

    return s;
}

int main(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "serve")) {
        fprintf(stderr, "usage: %s serve\n", argv[0]);
        return 2;
    }

    // sndio builds differ in the socket directory: the per-user one under
    // /tmp or /var/run, and the shared one next to it
    char dirs[4][64];

    snprintf(dirs[0], sizeof(dirs[0]), "/tmp/sndio-%u", (unsigned)geteuid());
    snprintf(dirs[1], sizeof(dirs[1]), "/tmp/sndio");
    snprintf(dirs[2], sizeof(dirs[2]), "/var/run/sndiod-%u", (unsigned)geteuid());
    snprintf(dirs[3], sizeof(dirs[3]), "/var/run/sndiod");

    int listeners[4];
    int listening = 0;

    for (int i = 0; i < 4; i++) {
        listeners[i] = listen_on(dirs[i]);
        listening += listeners[i] >= 0;
    }

    if (!listening) {
        fprintf(stderr, "neither /tmp nor /var/run can hold a socket\n");
        return 1;
    }

    if (mkfifo("sndio-ctl", 0600) < 0 && errno != EEXIST) {
        perror("sndio-ctl");
        return 1;
    }

    // held open for writing too, so the scenario's writers come and go
    // without an end of file
    int fifo = open("sndio-ctl", O_RDWR | O_CLOEXEC);
    char pending[256];
    size_t used = 0;

    event("ready");

    for (;;) {
        struct pollfd p[6];
        int n = 0;

        // the compositor first: what it wrote before a ping is read before it
        p[n++] = (struct pollfd){.fd = client, .events = POLLIN};
        p[n++] = (struct pollfd){.fd = fifo, .events = POLLIN};

        for (int i = 0; i < 4; i++) p[n++] = (struct pollfd){.fd = listeners[i], .events = POLLIN};

        if (poll(p, (nfds_t)n, -1) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }

        if (client >= 0 && (p[0].revents & (POLLIN | POLLHUP))) {
            while (client >= 0) {
                if (client_msg() < 0) {
                    close(client);
                    client = -1;
                    subscribed = 0;
                    event("gone");
                    break;
                }

                struct pollfd more = {.fd = client, .events = POLLIN};

                if (poll(&more, 1, 0) <= 0) break;
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

        for (int i = 0; i < 4; i++) {
            if (p[2 + i].revents & POLLIN) {
                int c = accept4(listeners[i], NULL, NULL, SOCK_CLOEXEC);

                if (c >= 0 && client >= 0) {
                    close(c);
                } else if (c >= 0) {
                    client = c;
                    event("connected");
                }
            }
        }
    }
}
