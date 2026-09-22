// Two devices on /dev/uinput, linked into the compositor's evdev directory:
// an absolute pointer with both wheels (the shape a VM's tablet device has)
// and a multitouch touchpad. Not a wayland client: the scenario pairs it
// with one that listens on the other end.
//
//   argv[1]  directory to link the nodes into
//   argv[2]  ABS_X to aim the pointer at, out of ABS_MAX_POS
//   argv[3]  ABS_Y likewise
//
// Phases come from files the scenario touches. Exits 0 and says so when the
// host has no usable uinput.

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

#define ABS_MAX_POS 32767

// a 100mm by 66mm pad: libinput wants a real size before it will look for
// gestures on it
#define PAD_W 3000
#define PAD_H 2000
#define PAD_RES 30
#define SLOTS 5

static const char* gDir;
static int gAbs = -1, gPad = -1;
static char gAbsNode[64], gPadNode[64];

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

static int absSetup(int fd, int code, int min, int max, int res) {
    struct uinput_abs_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.code = (unsigned short)code;
    setup.absinfo.minimum = min;
    setup.absinfo.maximum = max;
    setup.absinfo.resolution = res;

    return ioctl(fd, UI_ABS_SETUP, &setup);
}

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

static int create(int fd, const char* name, unsigned short bus, unsigned short product) {
    struct uinput_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = bus;
    setup.id.vendor = 0x0002;
    setup.id.product = product;
    snprintf(setup.name, sizeof(setup.name), "%s", name);

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0) {
        return -1;
    }

    return 0;
}

static int finish(int fd, char* node, size_t cap) {
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        return -1;
    }

    char sys[64] = {};

    if (ioctl(fd, UI_GET_SYSNAME(sizeof(sys)), sys) < 0 || eventName(sys, node, cap)) {
        return -1;
    }

    return 0;
}

// buttons, absolute X/Y and both wheels, no touch or tool bits: libinput
// takes it for a pointer that reports absolute positions
static int makeAbs(void) {
    static const int keys[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};

    gAbs = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (gAbs < 0) {
        return -1;
    }

    if (ioctl(gAbs, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(gAbs, UI_SET_EVBIT, EV_ABS) < 0 ||
        ioctl(gAbs, UI_SET_EVBIT, EV_REL) < 0 || ioctl(gAbs, UI_SET_ABSBIT, ABS_X) < 0 ||
        ioctl(gAbs, UI_SET_ABSBIT, ABS_Y) < 0 || ioctl(gAbs, UI_SET_RELBIT, REL_WHEEL) < 0 ||
        ioctl(gAbs, UI_SET_RELBIT, REL_HWHEEL) < 0) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (ioctl(gAbs, UI_SET_KEYBIT, keys[i]) < 0) {
            return -1;
        }
    }

    if (create(gAbs, "imway test absolute pointer", BUS_USB, 0x0010) ||
        absSetup(gAbs, ABS_X, 0, ABS_MAX_POS, 0) < 0 || absSetup(gAbs, ABS_Y, 0, ABS_MAX_POS, 0) < 0) {
        return -1;
    }

    return finish(gAbs, gAbsNode, sizeof(gAbsNode));
}

static int makePad(void) {
    static const int keys[] = {BTN_LEFT, BTN_TOUCH, BTN_TOOL_FINGER,
                               BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP};
    static const int axes[] = {ABS_X, ABS_Y, ABS_PRESSURE, ABS_MT_SLOT,
                               ABS_MT_POSITION_X, ABS_MT_POSITION_Y,
                               ABS_MT_TRACKING_ID, ABS_MT_PRESSURE};

    gPad = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (gPad < 0) {
        return -1;
    }

    if (ioctl(gPad, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0 ||
        ioctl(gPad, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(gPad, UI_SET_EVBIT, EV_ABS) < 0) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (ioctl(gPad, UI_SET_KEYBIT, keys[i]) < 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        if (ioctl(gPad, UI_SET_ABSBIT, axes[i]) < 0) {
            return -1;
        }
    }

    if (create(gPad, "imway test touchpad", BUS_I8042, 0x0007) ||
        absSetup(gPad, ABS_X, 0, PAD_W, PAD_RES) < 0 ||
        absSetup(gPad, ABS_Y, 0, PAD_H, PAD_RES) < 0 ||
        absSetup(gPad, ABS_PRESSURE, 0, 255, 0) < 0 ||
        absSetup(gPad, ABS_MT_SLOT, 0, SLOTS - 1, 0) < 0 ||
        absSetup(gPad, ABS_MT_POSITION_X, 0, PAD_W, PAD_RES) < 0 ||
        absSetup(gPad, ABS_MT_POSITION_Y, 0, PAD_H, PAD_RES) < 0 ||
        absSetup(gPad, ABS_MT_TRACKING_ID, 0, 65535, 0) < 0 ||
        absSetup(gPad, ABS_MT_PRESSURE, 0, 255, 0) < 0) {
        return -1;
    }

    return finish(gPad, gPadNode, sizeof(gPadNode));
}

static int linkNode(const char* name) {
    char node[128], link[256];

    snprintf(node, sizeof(node), "/dev/input/%s", name);
    snprintf(link, sizeof(link), "%s/%s", gDir, name);

    for (int i = 0; i < 60; i++) {
        if (access(node, R_OK) == 0) {
            unlink(link);

            return symlink(node, link);
        }

        usleep(50000);
    }

    printf("uinput unavailable: %s is not readable (%s)\n", node, strerror(errno));

    return -1;
}

static void dropNode(int fd, const char* name) {
    char link[256];

    snprintf(link, sizeof(link), "%s/%s", gDir, name);
    unlink(link);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

static int waitFor(const char* name) {
    for (int i = 0; i < 1200; i++) {
        if (access(name, F_OK) == 0) {
            return 0;
        }

        usleep(50000);
    }

    return -1;
}

// one finger of a frame: slot, then where it is
static int finger(int slot, int id, int x, int y) {
    if (emit(gPad, EV_ABS, ABS_MT_SLOT, slot)) return -1;

    if (id < 0) {
        return emit(gPad, EV_ABS, ABS_MT_TRACKING_ID, -1);
    }

    return emit(gPad, EV_ABS, ABS_MT_TRACKING_ID, id) ||
           emit(gPad, EV_ABS, ABS_MT_POSITION_X, x) ||
           emit(gPad, EV_ABS, ABS_MT_POSITION_Y, y) ||
           emit(gPad, EV_ABS, ABS_MT_PRESSURE, 60);
}

// two fingers down, moved together by (dx, dy) per frame for steps frames
// (none: they rest), then lifted
static int twoFingers(int dx, int dy, int steps, int restUs) {
    int x[2] = {1300, 1700}, y[2] = {1000, 1000};

    for (int i = 0; i < 2; i++) {
        if (finger(i, i + 1, x[i], y[i])) return -1;
    }

    if (emit(gPad, EV_KEY, BTN_TOUCH, 1) || emit(gPad, EV_KEY, BTN_TOOL_DOUBLETAP, 1) ||
        emit(gPad, EV_ABS, ABS_X, x[0]) || emit(gPad, EV_ABS, ABS_Y, y[0]) ||
        emit(gPad, EV_ABS, ABS_PRESSURE, 60) || syn(gPad)) {
        return -1;
    }

    usleep(restUs);

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < 2; i++) {
            x[i] += dx;
            y[i] += dy;

            if (finger(i, i + 1, x[i], y[i])) return -1;
        }

        if (emit(gPad, EV_ABS, ABS_X, x[0]) || emit(gPad, EV_ABS, ABS_Y, y[0]) || syn(gPad)) {
            return -1;
        }

        usleep(15000);
    }

    for (int i = 0; i < 2; i++) {
        if (finger(i, -1, 0, 0)) return -1;
    }

    return emit(gPad, EV_KEY, BTN_TOUCH, 0) || emit(gPad, EV_KEY, BTN_TOOL_DOUBLETAP, 0) || syn(gPad);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    gDir = argc > 1 ? argv[1] : "/dev/input";

    int aimX = argc > 2 ? atoi(argv[2]) : ABS_MAX_POS / 2;
    int aimY = argc > 3 ? atoi(argv[3]) : ABS_MAX_POS / 2;

    if (access("/dev/uinput", W_OK) != 0) {
        printf("uinput unavailable: /dev/uinput is not writable (%s)\n", strerror(errno));

        return 0;
    }

    if (makeAbs() || makePad()) {
        printf("uinput unavailable: cannot create the devices (%s)\n", strerror(errno));

        return 0;
    }

    printf("uinput created %s %s\n", gAbsNode, gPadNode);

    if (waitFor("go-link")) {
        fprintf(stderr, "the scenario never asked for the link\n");

        return 1;
    }

    if (linkNode(gAbsNode) || linkNode(gPadNode)) {
        ioctl(gAbs, UI_DEV_DESTROY);
        ioctl(gPad, UI_DEV_DESTROY);

        return 0;
    }

    printf("uinput ready %s %s\n", gAbsNode, gPadNode);

    // the far corner first, then onto the listener's window: an absolute
    // position lands where it says, no acceleration in between
    if (waitFor("go-aim")) {
        fprintf(stderr, "the scenario never asked to aim\n");

        return 1;
    }

    if (emit(gAbs, EV_ABS, ABS_X, ABS_MAX_POS) || emit(gAbs, EV_ABS, ABS_Y, ABS_MAX_POS) || syn(gAbs)) {
        return 1;
    }

    usleep(100000);

    if (emit(gAbs, EV_ABS, ABS_X, aimX) || emit(gAbs, EV_ABS, ABS_Y, aimY) || syn(gAbs)) {
        return 1;
    }

    printf("pointer aimed\n");

    // the enter needs a frame that has the window under the pointer: keep
    // nudging it around the aim point until the scenario has seen it land
    for (int i = 0; access("go-hwheel", F_OK) != 0; i++) {
        if (i == 1200) {
            fprintf(stderr, "the scenario never asked for the wheel\n");

            return 1;
        }

        int nudge = (i & 1) ? 64 : 0;

        if (emit(gAbs, EV_ABS, ABS_X, aimX + nudge) || emit(gAbs, EV_ABS, ABS_Y, aimY + nudge) || syn(gAbs)) {
            return 1;
        }

        usleep(50000);
    }

    for (int i = 0; i < 3; i++) {
        if (emit(gAbs, EV_REL, REL_HWHEEL, 1) || syn(gAbs)) {
            return 1;
        }

        usleep(30000);
    }

    printf("wheel tilted\n");

    if (waitFor("go-hold")) {
        fprintf(stderr, "the scenario never asked for the hold\n");

        return 1;
    }

    // two fingers resting, then lifted: a hold
    if (twoFingers(0, 0, 0, 600000)) {
        fprintf(stderr, "hold write failed: %s\n", strerror(errno));

        return 1;
    }

    printf("fingers held\n");

    if (waitFor("go-hscroll")) {
        fprintf(stderr, "the scenario never asked for the scroll\n");

        return 1;
    }

    // two fingers sliding sideways: a horizontal finger scroll
    if (twoFingers(-40, 0, 16, 30000)) {
        fprintf(stderr, "scroll write failed: %s\n", strerror(errno));

        return 1;
    }

    printf("fingers slid\n");

    if (waitFor("go-quit")) {
        fprintf(stderr, "the scenario never asked to stop\n");

        return 1;
    }

    dropNode(gAbs, gAbsNode);
    dropNode(gPad, gPadNode);
    printf("uinput unplugged\n");

    return 0;
}
