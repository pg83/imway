// A graphics tablet on /dev/uinput with the tools beyond the pen: an airbrush
// whose finger wheel is a slider, and a puck (the tablet mouse) with a
// rotating body and a scroll wheel. libinput turns them into tablet-tool
// axes and the compositor forwards those over tablet-v2. Not a wayland
// client: the scenario pairs it with one that listens on the other end.
//
//   argv[1]  directory to link the node into
//   argv[2]  ABS_X to aim the pen at, in the device's own units
//   argv[3]  ABS_Y likewise

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

// 200mm by 125mm at 100 units/mm, and a full ABS range of 20000 by 12500
#define TAB_W 20000
#define TAB_H 12500
#define TAB_RES 100

static const char* gDir;
static int gFd = -1;
static char gNode[64];
static int gX, gY;

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

static int makeTablet(void) {
    static const int keys[] = {BTN_TOOL_PEN, BTN_TOOL_AIRBRUSH, BTN_TOOL_MOUSE,
                               BTN_TOUCH, BTN_STYLUS, BTN_LEFT, BTN_RIGHT};
    static const int axes[] = {ABS_X, ABS_Y, ABS_PRESSURE, ABS_DISTANCE,
                               ABS_TILT_X, ABS_TILT_Y, ABS_WHEEL, ABS_Z};

    gFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (gFd < 0) {
        return -1;
    }

    if (ioctl(gFd, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0 ||
        ioctl(gFd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(gFd, UI_SET_EVBIT, EV_ABS) < 0 ||
        ioctl(gFd, UI_SET_EVBIT, EV_REL) < 0 || ioctl(gFd, UI_SET_RELBIT, REL_WHEEL) < 0) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (ioctl(gFd, UI_SET_KEYBIT, keys[i]) < 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        if (ioctl(gFd, UI_SET_ABSBIT, axes[i]) < 0) {
            return -1;
        }
    }

    struct uinput_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x056a; // a tablet vendor libwacom knows by name
    setup.id.product = 0x00de;
    snprintf(setup.name, sizeof(setup.name), "imway test tool tablet");

    if (ioctl(gFd, UI_DEV_SETUP, &setup) < 0) {
        return -1;
    }

    if (absSetup(ABS_X, 0, TAB_W, TAB_RES) < 0 ||
        absSetup(ABS_Y, 0, TAB_H, TAB_RES) < 0 ||
        absSetup(ABS_PRESSURE, 0, 1023, 0) < 0 ||
        absSetup(ABS_DISTANCE, 0, 63, 0) < 0 ||
        absSetup(ABS_TILT_X, -64, 63, 0) < 0 ||
        absSetup(ABS_TILT_Y, -64, 63, 0) < 0 ||
        absSetup(ABS_WHEEL, 0, 1023, 0) < 0 ||
        absSetup(ABS_Z, -900, 899, 0) < 0) {
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

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    gDir = argc > 1 ? argv[1] : "/dev/input";
    gX = argc > 2 ? atoi(argv[2]) : TAB_W / 2;
    gY = argc > 3 ? atoi(argv[3]) : TAB_H / 2;

    if (access("/dev/uinput", W_OK) != 0) {
        printf("uinput unavailable: /dev/uinput is not writable (%s)\n", strerror(errno));

        return 0;
    }

    if (makeTablet()) {
        printf("uinput unavailable: cannot create a tablet (%s)\n", strerror(errno));

        return 0;
    }

    printf("tablet created %s\n", gNode);

    if (waitFor("go-link")) {
        fprintf(stderr, "the scenario never asked for the link\n");

        return 1;
    }

    if (linkNode()) {
        ioctl(gFd, UI_DEV_DESTROY);

        return 0;
    }

    printf("tablet ready %s\n", gNode);

    if (waitFor("go-tools")) {
        fprintf(stderr, "the scenario never asked for the tools\n");

        return 1;
    }

    // the airbrush comes into range over the window and rolls its wheel
    if (emit(EV_KEY, BTN_TOOL_AIRBRUSH, 1) || emit(EV_ABS, ABS_X, gX) ||
        emit(EV_ABS, ABS_Y, gY) || emit(EV_ABS, ABS_DISTANCE, 40) ||
        emit(EV_ABS, ABS_WHEEL, 100) || syn()) {
        fprintf(stderr, "airbrush write failed: %s\n", strerror(errno));

        return 1;
    }

    usleep(50000);

    for (int i = 1; i <= 6; i++) {
        if (emit(EV_ABS, ABS_X, gX + i * 20) || emit(EV_ABS, ABS_WHEEL, 100 + i * 120) || syn()) {
            fprintf(stderr, "slider write failed: %s\n", strerror(errno));

            return 1;
        }

        usleep(20000);
    }

    if (emit(EV_KEY, BTN_TOOL_AIRBRUSH, 0) || emit(EV_ABS, ABS_DISTANCE, 63) || syn()) {
        return 1;
    }

    usleep(100000);

    // then the puck: in range, turned, its wheel scrolled
    if (emit(EV_KEY, BTN_TOOL_MOUSE, 1) || emit(EV_ABS, ABS_X, gX) ||
        emit(EV_ABS, ABS_Y, gY) || emit(EV_ABS, ABS_DISTANCE, 20) ||
        emit(EV_ABS, ABS_Z, 0) || syn()) {
        fprintf(stderr, "puck write failed: %s\n", strerror(errno));

        return 1;
    }

    usleep(50000);

    for (int i = 1; i <= 6; i++) {
        if (emit(EV_ABS, ABS_Y, gY + i * 12) || emit(EV_ABS, ABS_Z, i * 100) ||
            emit(EV_REL, REL_WHEEL, 1) || syn()) {
            fprintf(stderr, "rotation write failed: %s\n", strerror(errno));

            return 1;
        }

        usleep(20000);
    }

    if (emit(EV_KEY, BTN_TOOL_MOUSE, 0) || emit(EV_ABS, ABS_DISTANCE, 63) || syn()) {
        return 1;
    }

    printf("tools done\n");

    if (waitFor("go-quit")) {
        fprintf(stderr, "the scenario never asked to stop\n");

        return 1;
    }

    char link[256];

    snprintf(link, sizeof(link), "%s/%s", gDir, gNode);
    unlink(link);
    ioctl(gFd, UI_DEV_DESTROY);
    close(gFd);
    printf("tablet unplugged\n");

    return 0;
}
