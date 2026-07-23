// ICC v2/v4 feature: a real Display-P3 matrix-shaper profile is accepted and
// its transfer curves and colorants change the composited pixels.

#include "wl_util.h"

#include <color-management-v1-client-protocol.h>
#include <lcms2.h>

#include <vector>

static wp_color_manager_v1* manager;
static bool gotIccFeature;
static bool managerDone;
static bool descriptionReady;
static bool descriptionFailed;

static void managerIntent(void*, wp_color_manager_v1*, uint32_t) {
}

static void managerFeature(void*, wp_color_manager_v1*, uint32_t feature) {
    gotIccFeature |= feature == WP_COLOR_MANAGER_V1_FEATURE_ICC_V2_V4;
}

static void managerTf(void*, wp_color_manager_v1*, uint32_t) {
}

static void managerPrimaries(void*, wp_color_manager_v1*, uint32_t) {
}

static void managerDoneEvent(void*, wp_color_manager_v1*) {
    managerDone = true;
}

static const wp_color_manager_v1_listener managerListener = {
    .supported_intent = managerIntent,
    .supported_feature = managerFeature,
    .supported_tf_named = managerTf,
    .supported_primaries_named = managerPrimaries,
    .done = managerDoneEvent,
};

static void descriptionFailedEvent(void*, wp_image_description_v1*, uint32_t, const char* message) {
    fprintf(stderr, "ICC image description failed: %s\n", message);
    descriptionFailed = true;
}

static void descriptionReadyEvent(void*, wp_image_description_v1*, uint32_t) {
    descriptionReady = true;
}

static void descriptionReady2Event(void*, wp_image_description_v1*, uint32_t, uint32_t) {
    descriptionReady = true;
}

static const wp_image_description_v1_listener descriptionListener = {
    .failed = descriptionFailedEvent,
    .ready = descriptionReadyEvent,
    .ready2 = descriptionReady2Event,
};

static void extraGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (!strcmp(interface, wp_color_manager_v1_interface.name)) {
        manager = (wp_color_manager_v1*)wl_registry_bind(registry, name, &wp_color_manager_v1_interface, version < 3 ? version : 3);
    }
}

static void extraRemove(void*, wl_registry*, uint32_t) {
}

static const wl_registry_listener extraListener = {extraGlobal, extraRemove};

static int p3ProfileFd(uint32_t& offset, uint32_t& length) {
    cmsCIExyY white{0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries{
        {0.680, 0.320, 1.0},
        {0.265, 0.690, 1.0},
        {0.150, 0.060, 1.0},
    };
    cmsToneCurve* gamma = cmsBuildGamma(nullptr, 2.2);
    cmsToneCurve* curves[3] = {gamma, gamma, gamma};
    cmsHPROFILE profile = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(gamma);

    cmsUInt32Number bytes = 0;
    if (!profile || !cmsSaveProfileToMem(profile, nullptr, &bytes)) {
        fprintf(stderr, "cannot size test ICC profile\n");
        exit(2);
    }

    std::vector<unsigned char> data(bytes);
    if (!cmsSaveProfileToMem(profile, data.data(), &bytes)) {
        fprintf(stderr, "cannot serialize test ICC profile\n");
        exit(2);
    }
    cmsCloseProfile(profile);

    offset = 137;
    length = bytes;
    int fd = memfd_create("icc-display-p3", 0);
    if (fd < 0 || ftruncate(fd, (off_t)offset + length + 29) < 0 || pwrite(fd, data.data(), length, offset) != (ssize_t)length) {
        perror("test ICC memfd");
        exit(2);
    }

    return fd;
}

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    alarm(10);
    if (wl_boot()) {
        return 2;
    }

    wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extraListener, nullptr);
    wl_display_roundtrip(wl_dpy);
    if (!manager) {
        fprintf(stderr, "missing color manager\n");
        return 2;
    }

    wp_color_manager_v1_add_listener(manager, &managerListener, nullptr);
    wl_display_roundtrip(wl_dpy);
    if (!managerDone || !gotIccFeature) {
        fprintf(stderr, "ICC feature was not advertised\n");
        return 1;
    }

    wl_toplevel_ctx top{};
    wl_make_toplevel(&top, "client_reg_color_icc", 300, 200, 0xFFB4783C);
    puts("client_reg_color_icc: raw");
    sleep(1);

    uint32_t offset = 0, length = 0;
    int fd = p3ProfileFd(offset, length);
    wp_image_description_creator_icc_v1* creator = wp_color_manager_v1_create_icc_creator(manager);
    wp_image_description_creator_icc_v1_set_icc_file(creator, fd, offset, length);
    close(fd);
    wp_image_description_v1* description = wp_image_description_creator_icc_v1_create(creator);
    wp_image_description_v1_add_listener(description, &descriptionListener, nullptr);

    while (!descriptionReady && !descriptionFailed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!descriptionReady || descriptionFailed) {
        return 1;
    }

    wp_color_management_surface_v1* colorSurface = wp_color_manager_v1_get_surface(manager, top.surface);
    wp_color_management_surface_v1_set_image_description(colorSurface, description, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
    wl_surface_commit(top.surface);
    puts("client_reg_color_icc: managed");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
