// A multitouch touchpad on /dev/uinput, linked into the compositor's evdev
// directory so libinput picks it up and turns finger movement into the swipe
// and pinch gestures the desktop binds actions to. Not a wayland client: it
// drives the machine end while the scenario watches what the compositor
// does with it.
//
// argv[1] is the directory to link the node into. Phases come from files the
// scenario touches. Exits 0 and says so when the host has no usable uinput.

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

// a 100mm by 66mm pad: libinput wants a real size before it will look for
// gestures on it
#define PAD_W 3000
#define PAD_H 2000
#define PAD_RES 30
#define SLOTS 5

static const char* gDir;
static int gFd = -1;
static char gNode[64];

static int emit(unsigned type, unsigned code, int value) {
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;

    return write(gFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}

static int syn(void) {
    return emit(EV_SYN, SYN_REPORT, 0);
}

static int absSetup(int code, int min, int max, int res) {
    struct uinput_abs_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.code = (unsigned short)code;
    setup.absinfo.minimum = min;
    setup.absinfo.maximum = max;
    setup.absinfo.resolution = res;

    return ioctl(gFd, UI_ABS_SETUP, &setup);
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

static int makePad(void) {
    static const int keys[] = {BTN_LEFT, BTN_TOUCH, BTN_TOOL_FINGER,
                               BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP};

    gFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (gFd < 0) {
        return -1;
    }

    if (ioctl(gFd, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0 ||
        ioctl(gFd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(gFd, UI_SET_EVBIT, EV_ABS) < 0) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (ioctl(gFd, UI_SET_KEYBIT, keys[i]) < 0) {
            return -1;
        }
    }

    static const int axes[] = {ABS_X, ABS_Y, ABS_PRESSURE, ABS_MT_SLOT,
                               ABS_MT_POSITION_X, ABS_MT_POSITION_Y,
                               ABS_MT_TRACKING_ID, ABS_MT_PRESSURE};

    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        if (ioctl(gFd, UI_SET_ABSBIT, axes[i]) < 0) {
            return -1;
        }
    }

    struct uinput_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_I8042;
    setup.id.vendor = 0x0002;
    setup.id.product = 0x0007;
    snprintf(setup.name, sizeof(setup.name), "imway test touchpad");

    if (ioctl(gFd, UI_DEV_SETUP, &setup) < 0) {
        return -1;
    }

    if (absSetup(ABS_X, 0, PAD_W, PAD_RES) < 0 ||
        absSetup(ABS_Y, 0, PAD_H, PAD_RES) < 0 ||
        absSetup(ABS_PRESSURE, 0, 255, 0) < 0 ||
        absSetup(ABS_MT_SLOT, 0, SLOTS - 1, 0) < 0 ||
        absSetup(ABS_MT_POSITION_X, 0, PAD_W, PAD_RES) < 0 ||
        absSetup(ABS_MT_POSITION_Y, 0, PAD_H, PAD_RES) < 0 ||
        absSetup(ABS_MT_TRACKING_ID, 0, 65535, 0) < 0 ||
        absSetup(ABS_MT_PRESSURE, 0, 255, 0) < 0) {
        return -1;
    }

    if (ioctl(gFd, UI_DEV_CREATE) < 0) {
        return -1;
    }

    char sys[64] = {};

    if (ioctl(gFd, UI_GET_SYSNAME(sizeof(sys)), sys) < 0 ||
        eventName(sys, gNode, sizeof(gNode))) {
        return -1;
    }

    return 0;
}

static int linkNode(void) {
    char node[128], link[256];

    snprintf(node, sizeof(node), "/dev/input/%s", gNode);
    snprintf(link, sizeof(link), "%s/%s", gDir, gNode);

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
    if (emit(EV_ABS, ABS_MT_SLOT, slot)) return -1;

    if (id < 0) {
        return emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    }

    return emit(EV_ABS, ABS_MT_TRACKING_ID, id) ||
           emit(EV_ABS, ABS_MT_POSITION_X, x) ||
           emit(EV_ABS, ABS_MT_POSITION_Y, y) ||
           emit(EV_ABS, ABS_MT_PRESSURE, 60);
}

// n fingers down at their starting places, then moved together or apart over
// a run of frames, then lifted
static int gesture(int fingers, int startX[], int startY[], int dx[], int dy[], int steps) {
    static const int tool[] = {0, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP};
    int x[4], y[4];

    for (int i = 0; i < fingers; i++) {
        x[i] = startX[i];
        y[i] = startY[i];

        if (finger(i, i + 1, x[i], y[i])) return -1;
    }

    if (emit(EV_KEY, BTN_TOUCH, 1) || emit(EV_KEY, tool[fingers], 1) ||
        emit(EV_ABS, ABS_X, x[0]) || emit(EV_ABS, ABS_Y, y[0]) ||
        emit(EV_ABS, ABS_PRESSURE, 60) || syn()) {
        return -1;
    }

    usleep(30000);

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < fingers; i++) {
            x[i] += dx[i];
            y[i] += dy[i];

            if (finger(i, i + 1, x[i], y[i])) return -1;
        }

        if (emit(EV_ABS, ABS_X, x[0]) || emit(EV_ABS, ABS_Y, y[0]) || syn()) {
            return -1;
        }

        usleep(15000);
    }

    for (int i = 0; i < fingers; i++) {
        if (finger(i, -1, 0, 0)) return -1;
    }

    return emit(EV_KEY, BTN_TOUCH, 0) || emit(EV_KEY, tool[fingers], 0) || syn();
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    gDir = argc > 1 ? argv[1] : "/dev/input";

    if (access("/dev/uinput", W_OK) != 0) {
        printf("uinput unavailable: /dev/uinput is not writable (%s)\n", strerror(errno));

        return 0;
    }

    if (makePad()) {
        printf("uinput unavailable: cannot create a touchpad (%s)\n", strerror(errno));

        return 0;
    }

    printf("touchpad created %s\n", gNode);

    if (waitFor("go-link")) {
        fprintf(stderr, "the scenario never asked for the link\n");

        return 1;
    }

    if (linkNode()) {
        ioctl(gFd, UI_DEV_DESTROY);

        return 0;
    }

    printf("touchpad ready %s\n", gNode);

    if (waitFor("go-swipe")) {
        fprintf(stderr, "the scenario never asked for the swipe\n");

        return 1;
    }

    // three fingers travelling up the pad
    {
        int sx[3] = {1000, 1400, 1800};
        int sy[3] = {1600, 1600, 1600};
        int dx[3] = {0, 0, 0};
        int dy[3] = {-60, -60, -60};

        if (gesture(3, sx, sy, dx, dy, 14)) {
            fprintf(stderr, "swipe write failed: %s\n", strerror(errno));

            return 1;
        }
    }

    printf("swiped up\n");

    if (waitFor("go-pinch")) {
        fprintf(stderr, "the scenario never asked for the pinch\n");

        return 1;
    }

    // two fingers moving apart
    {
        int sx[2] = {1400, 1600};
        int sy[2] = {1000, 1000};
        int dx[2] = {-50, 50};
        int dy[2] = {0, 0};

        if (gesture(2, sx, sy, dx, dy, 14)) {
            fprintf(stderr, "pinch write failed: %s\n", strerror(errno));

            return 1;
        }
    }

    printf("pinched out\n");

    if (waitFor("go-quit")) {
        fprintf(stderr, "the scenario never asked to stop\n");

        return 1;
    }

    char link[256];

    snprintf(link, sizeof(link), "%s/%s", gDir, gNode);
    unlink(link);
    ioctl(gFd, UI_DEV_DESTROY);
    close(gFd);
    printf("touchpad unplugged\n");

    return 0;
}
