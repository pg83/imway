// A virtual keyboard and a virtual mouse on /dev/uinput, symlinked into the
// compositor's own evdev directory so its libinput source picks them up
// through inotify exactly as it would a real plug. Not a wayland client: it
// drives the machine end of the input stack while the scenario watches what
// comes out of the compositor.
//
// argv[1] is the directory to link the nodes into. Phases are taken from
// files the scenario touches, so nothing here depends on a sleep being long
// enough. Exits 0 and says so on stdout when the host has no usable uinput,
// which is every sandbox that does not hand it out.

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>

static const char* gDir;

static int emit(int fd, unsigned type, unsigned code, int value) {
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;

    return write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}

static int syn(int fd) {
    return emit(fd, EV_SYN, SYN_REPORT, 0);
}

// the kernel publishes the evdev node as a child directory of the input
// device; it appears a moment after UI_DEV_CREATE returns
static int eventName(const char* inputName, char* out, size_t cap) {
    char dir[128];

    snprintf(dir, sizeof(dir), "/sys/class/input/%s", inputName);

    for (int i = 0; i < 100; i++) {
        DIR* d = opendir(dir);

        if (d) {
            struct dirent* e;

            while ((e = readdir(d))) {
                if (!strncmp(e->d_name, "event", 5) && e->d_name[5]) {
                    snprintf(out, cap, "%s", e->d_name);
                    closedir(d);

                    return 0;
                }
            }

            closedir(d);
        }

        usleep(20000);
    }

    return -1;
}

// create one device, then link the node it grew into the compositor's
// directory under the same name, so the index libinput reports for it and
// the one the compositor watches agree
static int makeDevice(const char* name, const int* keys, int nkeys, int rel, char* sysname, size_t cap) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0) {
        return -1;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        close(fd);

        return -1;
    }

    for (int i = 0; i < nkeys; i++) {
        if (ioctl(fd, UI_SET_KEYBIT, keys[i]) < 0) {
            close(fd);

            return -1;
        }
    }

    if (rel) {
        if (ioctl(fd, UI_SET_EVBIT, EV_REL) < 0 ||
            ioctl(fd, UI_SET_RELBIT, REL_X) < 0 ||
            ioctl(fd, UI_SET_RELBIT, REL_Y) < 0 ||
            ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0) {
            close(fd);

            return -1;
        }
    }

    struct uinput_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1d6b;
    setup.id.product = rel ? 0x0002 : 0x0001;
    snprintf(setup.name, sizeof(setup.name), "%s", name);

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);

        return -1;
    }

    // UI_GET_SYSNAME names the input device, "inputN". The evdev node the
    // path backend opens is a child of it in sysfs, and its name is the one
    // libinput reports back as the device's sysname.
    char sys[64] = {};

    if (ioctl(fd, UI_GET_SYSNAME(sizeof(sys)), sys) < 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);

        return -1;
    }

    if (eventName(sys, sysname, cap)) {
        fprintf(stderr, "no evdev node under /sys/class/input/%s\n", sys);
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);

        return -1;
    }

    return fd;
}

// Link the node into the compositor's directory, once it is ours to open.
// Whoever manages /dev decides that, and it is not instant: udev applies
// its rules after the kernel has already created the node.
static int linkDevice(const char* sysname) {
    char node[256], link[512];

    snprintf(node, sizeof(node), "/dev/input/%s", sysname);
    snprintf(link, sizeof(link), "%s/%s", gDir, sysname);

    int readable = 0;

    for (int i = 0; i < 60 && !readable; i++) {
        readable = access(node, R_OK) == 0;

        if (!readable) {
            usleep(50000);
        }
    }

    if (!readable) {
        printf("uinput unavailable: %s is not readable (%s)\n", node, strerror(errno));

        return -1;
    }

    unlink(link);

    return symlink(node, link);
}

static void dropDevice(int fd, const char* sysname) {
    char link[512];

    snprintf(link, sizeof(link), "%s/%s", gDir, sysname);
    unlink(link);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

// the scenario touches these; polling a file keeps the phases in step
// without a guessed sleep on either side
static int waitFor(const char* name) {
    for (int i = 0; i < 1200; i++) {
        if (access(name, F_OK) == 0) {
            return 0;
        }

        usleep(50000);
    }

    return -1;
}

static const char* waitForEither(const char* a, const char* b) {
    for (int i = 0; i < 1200; i++) {
        if (access(a, F_OK) == 0) {
            return a;
        }

        if (access(b, F_OK) == 0) {
            return b;
        }

        usleep(50000);
    }

    return NULL;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    gDir = argc > 1 ? argv[1] : "/dev/input";

    if (access("/dev/uinput", W_OK) != 0) {
        printf("uinput unavailable: /dev/uinput is not writable (%s)\n", strerror(errno));

        return 0;
    }

    static const int kbdKeys[] = {KEY_LEFTMETA, KEY_F2, KEY_ESC, KEY_A};
    static const int mouseKeys[] = {BTN_LEFT, BTN_RIGHT};
    char kbdName[64] = {}, mouseName[64] = {};

    int kbd = makeDevice("imway test keyboard", kbdKeys, 4, 0, kbdName, sizeof(kbdName));

    if (kbd < 0) {
        printf("uinput unavailable: cannot create a virtual keyboard (%s)\n", strerror(errno));

        return 0;
    }

    int mouse = makeDevice("imway test mouse", mouseKeys, 2, 1, mouseName, sizeof(mouseName));

    if (mouse < 0) {
        printf("uinput unavailable: cannot create a virtual mouse (%s)\n", strerror(errno));
        dropDevice(kbd, kbdName);

        return 0;
    }

    // the nodes exist now but are not linked yet: the scenario gets its
    // chance to make them openable before the compositor is shown them
    printf("uinput created %s %s\n", kbdName, mouseName);

    if (waitFor("go-link")) {
        fprintf(stderr, "the scenario never asked for the link\n");

        return 1;
    }

    if (linkDevice(kbdName) || linkDevice(mouseName)) {
        dropDevice(mouse, mouseName);
        dropDevice(kbd, kbdName);

        return 0;
    }

    printf("uinput ready %s %s\n", kbdName, mouseName);

    // Relative motion is accelerated on its way through libinput, so the
    // pixels a delta becomes are not ours to predict. Every move here is
    // large enough to clamp against an edge, which makes where the cursor
    // ends up exact without assuming anything about the curve.
    if (waitFor("go-far")) {
        fprintf(stderr, "the scenario never asked for the far corner\n");

        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (emit(mouse, EV_REL, REL_X, 200) || emit(mouse, EV_REL, REL_Y, 200) || syn(mouse)) {
            fprintf(stderr, "relative motion write failed: %s\n", strerror(errno));

            return 1;
        }
    }

    // the empty bottom-right corner is a safe place to press a button
    if (emit(mouse, EV_KEY, BTN_LEFT, 1) || syn(mouse) ||
        emit(mouse, EV_KEY, BTN_LEFT, 0) || syn(mouse) ||
        emit(mouse, EV_REL, REL_WHEEL, -1) || syn(mouse)) {
        fprintf(stderr, "button write failed: %s\n", strerror(errno));

        return 1;
    }

    printf("pointer far\n");

    if (waitFor("go-corner")) {
        fprintf(stderr, "the scenario never asked for the corner\n");

        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (emit(mouse, EV_REL, REL_X, -200) || emit(mouse, EV_REL, REL_Y, -200) || syn(mouse)) {
            fprintf(stderr, "relative motion write failed: %s\n", strerror(errno));

            return 1;
        }
    }

    printf("pointer cornered\n");

    if (waitFor("go-keys")) {
        fprintf(stderr, "the scenario never asked for the keys\n");

        return 1;
    }

    if (emit(kbd, EV_KEY, KEY_LEFTMETA, 1) || syn(kbd) ||
        emit(kbd, EV_KEY, KEY_F2, 1) || syn(kbd) ||
        emit(kbd, EV_KEY, KEY_F2, 0) || syn(kbd) ||
        emit(kbd, EV_KEY, KEY_LEFTMETA, 0) || syn(kbd)) {
        fprintf(stderr, "key write failed: %s\n", strerror(errno));

        return 1;
    }

    printf("keys sent\n");

    // a scenario may relink the keyboard before the unplug: its node leaves
    // the directory and comes back as the same device (linking unlinks
    // first), and both inotify events can land in one read
    const char* next = waitForEither("go-relink", "go-unplug");

    if (!next) {
        fprintf(stderr, "the scenario never asked for the relink or the unplug\n");

        return 1;
    }

    if (!strcmp(next, "go-relink")) {
        if (linkDevice(kbdName)) {
            fprintf(stderr, "the keyboard could not be linked again\n");

            return 1;
        }

        printf("keyboard relinked\n");

        if (waitFor("go-escape")) {
            fprintf(stderr, "the scenario never asked for the escape\n");

            return 1;
        }

        if (emit(kbd, EV_KEY, KEY_ESC, 1) || syn(kbd) || emit(kbd, EV_KEY, KEY_ESC, 0) || syn(kbd)) {
            fprintf(stderr, "key write failed: %s\n", strerror(errno));

            return 1;
        }

        printf("escape sent\n");
    }

    if (waitFor("go-unplug")) {
        fprintf(stderr, "the scenario never asked for the unplug\n");

        return 1;
    }

    dropDevice(mouse, mouseName);
    dropDevice(kbd, kbdName);
    printf("devices unplugged\n");

    return 0;
}
