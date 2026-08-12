// Fatal and asynchronous ICC protocol validation. Each fatal mode is run in
// a fresh process by the scenario.

#include "wl_util.h"

#include <color-management-v1-client-protocol.h>
#include <lcms2.h>

#include <vector>

static wp_color_manager_v1* manager;
static bool failed;
static bool ready;
static uint32_t failedCause;

static void descriptionFailed(void*, wp_image_description_v1*, uint32_t cause, const char*) {
    failed = true;
    failedCause = cause;
}

static void descriptionReady(void*, wp_image_description_v1*, uint32_t) {
    ready = true;
}

static void descriptionReady2(void*, wp_image_description_v1*, uint32_t, uint32_t) {
    ready = true;
}

static const wp_image_description_v1_listener descriptionListener = {
    .failed = descriptionFailed,
    .ready = descriptionReady,
    .ready2 = descriptionReady2,
};

static void extraGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (!strcmp(interface, wp_color_manager_v1_interface.name)) {
        manager = (wp_color_manager_v1*)wl_registry_bind(registry, name, &wp_color_manager_v1_interface, version < 3 ? version : 3);
    }
}

static void extraRemove(void*, wl_registry*, uint32_t) {
}

static const wl_registry_listener extraListener = {extraGlobal, extraRemove};

static int expectCreatorError(uint32_t code) {
    (void)wl_display_roundtrip(wl_dpy);
    const wl_interface* interface = nullptr;
    uint32_t object = 0;
    uint32_t actual = wl_display_get_protocol_error(wl_dpy, &interface, &object);
    if (wl_display_get_error(wl_dpy) != EPROTO || actual != code) {
        fprintf(stderr, "wrong ICC creator error %u, want %u (interface=%s object=%u)\n", actual, code, interface ? interface->name : "?", object);
        return 1;
    }
    return 0;
}

static int dataFd(size_t size) {
    int fd = memfd_create("icc-validation", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("validation memfd");
        exit(2);
    }
    return fd;
}

static int profileFd(cmsProfileClassSignature profileClass, uint32_t& size) {
    cmsHPROFILE profile = cmsCreate_sRGBProfile();
    cmsSetDeviceClass(profile, profileClass);
    cmsUInt32Number bytes = 0;
    cmsSaveProfileToMem(profile, nullptr, &bytes);
    std::vector<unsigned char> data(bytes);
    cmsSaveProfileToMem(profile, data.data(), &bytes);
    cmsCloseProfile(profile);

    int fd = dataFd(bytes);
    if (pwrite(fd, data.data(), bytes, 0) != (ssize_t)bytes) {
        return -1;
    }
    size = bytes;
    return fd;
}

int main(int argc, char** argv) {
    alarm(10);
    if (argc != 2 || wl_boot()) {
        return 2;
    }
    wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extraListener, nullptr);
    wl_display_roundtrip(wl_dpy);
    if (!manager) {
        return 2;
    }

    wp_image_description_creator_icc_v1* creator = wp_color_manager_v1_create_icc_creator(manager);

    if (!strcmp(argv[1], "incomplete")) {
        wp_image_description_creator_icc_v1_create(creator);
        return expectCreatorError(WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_INCOMPLETE_SET);
    }

    if (!strcmp(argv[1], "bad-fd")) {
        int pipeFd[2];
        if (pipe(pipeFd)) {
            return 2;
        }
        wp_image_description_creator_icc_v1_set_icc_file(creator, pipeFd[0], 0, 4);
        close(pipeFd[0]);
        close(pipeFd[1]);
        return expectCreatorError(WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD);
    }

    int fd = dataFd(64);
    if (!strcmp(argv[1], "bad-size-zero")) {
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 0, 0);
        close(fd);
        return expectCreatorError(WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_SIZE);
    }
    if (!strcmp(argv[1], "bad-size-large")) {
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 0, 32 * 1024 * 1024 + 1);
        close(fd);
        return expectCreatorError(WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_SIZE);
    }
    if (!strcmp(argv[1], "out-of-file")) {
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 32, 33);
        close(fd);
        return expectCreatorError(WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_OUT_OF_FILE);
    }
    if (!strcmp(argv[1], "duplicate")) {
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 0, 64);
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 0, 64);
        close(fd);
        return expectCreatorError(WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_ALREADY_SET);
    }
    if (!strcmp(argv[1], "invalid-profile")) {
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 0, 64);
        close(fd);
        wp_image_description_v1* description = wp_image_description_creator_icc_v1_create(creator);
        wp_image_description_v1_add_listener(description, &descriptionListener, nullptr);
        wl_display_roundtrip(wl_dpy);
        return !failed || failedCause != WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED;
    }

    if (!strcmp(argv[1], "invalid-class") || !strcmp(argv[1], "information")) {
        close(fd);
        uint32_t size = 0;
        fd = profileFd(!strcmp(argv[1], "invalid-class") ? cmsSigInputClass : cmsSigDisplayClass, size);
        if (fd < 0) {
            return 2;
        }
        wp_image_description_creator_icc_v1_set_icc_file(creator, fd, 0, size);
        close(fd);
        wp_image_description_v1* description = wp_image_description_creator_icc_v1_create(creator);
        wp_image_description_v1_add_listener(description, &descriptionListener, nullptr);
        wl_display_roundtrip(wl_dpy);

        if (!strcmp(argv[1], "invalid-class")) {
            return !failed || failedCause != WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED;
        }
        if (!ready || failed) {
            return 1;
        }
        wp_image_description_v1_get_information(description);
        return wl_expect_error(wp_image_description_v1_interface.name, WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION);
    }

    close(fd);
    return 2;
}
