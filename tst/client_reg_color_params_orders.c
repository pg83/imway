#include "wl_util.h"

#include <color-management-v1-client-protocol.h>
#include <color-representation-v1-client-protocol.h>

// Parametric image descriptions the other colour tests leave out: every
// advertised named primaries set beyond sRGB/BT.2020/P3/DCI becomes a ready
// description; luminances set after a PQ or an extended-linear transfer and
// before a BT.1886 one are taken in either order. An ICC profile that cannot
// be read makes a description that fails. Then the objects that only
// ever die with their client die on request: a colour-management output, a
// surface feedback, the colour representation manager.

static struct wp_color_manager_v1* cm;
static struct wp_color_representation_manager_v1* representation;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_color_representation_manager_v1_interface.name))
        representation = wl_registry_bind(r, name, &wp_color_representation_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int desc_ready, desc_failed;
static uint32_t desc_cause;

static void desc_ready_cb(void* d, struct wp_image_description_v1* i, uint32_t identity) {
    (void)d; (void)i; (void)identity;
    desc_ready = 1;
}
static void desc_failed_cb(void* d, struct wp_image_description_v1* i,
                           uint32_t cause, const char* msg) {
    (void)d; (void)i;
    fprintf(stderr, "image description failed: %s\n", msg ? msg : "?");
    desc_failed = 1;
    desc_cause = cause;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed_cb,
    .ready = desc_ready_cb,
};

static void create(const char* what, struct wp_image_description_creator_params_v1* params) {
    struct wp_image_description_v1* desc = wp_image_description_creator_params_v1_create(params);

    desc_ready = desc_failed = 0;
    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);

    while (!desc_ready && !desc_failed && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!desc_ready) {
        fprintf(stderr, "%s: the description never became ready\n", what);
        exit(1);
    }

    wp_image_description_v1_destroy(desc);
    printf("%s: ready\n", what);
}

static struct wp_image_description_creator_params_v1* params(void) {
    return wp_color_manager_v1_create_parametric_creator(cm);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!cm || !representation || !output) return 2;

    static const struct {
        const char* name;
        uint32_t primaries;
    } named[] = {
        {"pal-m", WP_COLOR_MANAGER_V1_PRIMARIES_PAL_M},
        {"pal", WP_COLOR_MANAGER_V1_PRIMARIES_PAL},
        {"ntsc", WP_COLOR_MANAGER_V1_PRIMARIES_NTSC},
        {"generic-film", WP_COLOR_MANAGER_V1_PRIMARIES_GENERIC_FILM},
        {"adobe-rgb", WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB},
    };

    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        struct wp_image_description_creator_params_v1* p = params();

        wp_image_description_creator_params_v1_set_tf_named(p, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);
        wp_image_description_creator_params_v1_set_primaries_named(p, named[i].primaries);
        create(named[i].name, p);
    }

    // luminances after the transfer function: PQ and extended linear each
    // derive their own range from them
    static const struct {
        const char* name;
        uint32_t tf;
    } after[] = {
        {"pq then luminances", WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ},
        {"linear then luminances", WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR},
    };

    for (size_t i = 0; i < sizeof(after) / sizeof(after[0]); i++) {
        struct wp_image_description_creator_params_v1* p = params();

        wp_image_description_creator_params_v1_set_tf_named(p, after[i].tf);
        wp_image_description_creator_params_v1_set_primaries_named(p, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
        wp_image_description_creator_params_v1_set_luminances(p, 50, 1000, 203);
        create(after[i].name, p);
    }

    // BT.1886 after the luminances keeps them instead of its defaults
    struct wp_image_description_creator_params_v1* p = params();

    wp_image_description_creator_params_v1_set_luminances(p, 1000, 300, 100);
    wp_image_description_creator_params_v1_set_tf_named(p, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886);
    wp_image_description_creator_params_v1_set_primaries_named(p, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
    create("luminances then bt1886", p);

    // an ICC profile the compositor cannot read: a directory is readable,
    // seekable and has a size, but reading it fails, and the description
    // fails with an operating-system cause instead of a protocol error
    int dir = open(getenv("XDG_RUNTIME_DIR"), O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if (dir < 0) return 2;

    struct wp_image_description_creator_icc_v1* icc = wp_color_manager_v1_create_icc_creator(cm);

    wp_image_description_creator_icc_v1_set_icc_file(icc, dir, 0, 1);
    close(dir);

    struct wp_image_description_v1* unreadable = wp_image_description_creator_icc_v1_create(icc);

    desc_ready = desc_failed = 0;
    wp_image_description_v1_add_listener(unreadable, &desc_listener, NULL);

    while (!desc_ready && !desc_failed && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!desc_failed || desc_cause != WP_IMAGE_DESCRIPTION_V1_CAUSE_OPERATING_SYSTEM) {
        fprintf(stderr, "an unreadable ICC profile: ready=%d failed=%d cause=%u error=%d\n", desc_ready,
                desc_failed, desc_cause, wl_display_get_error(wl_dpy));
        return 1;
    }

    wp_image_description_v1_destroy(unreadable);
    printf("unreadable icc: failed\n");

    wp_color_management_output_v1_destroy(wp_color_manager_v1_get_output(cm, output));
    wp_color_management_surface_feedback_v1_destroy(
        wp_color_manager_v1_get_surface_feedback(cm, wl_compositor_create_surface(wl_comp)));
    wp_color_representation_manager_v1_destroy(representation);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "destroying the colour objects failed\n");
        return 1;
    }

    printf("colour params done\n");

    return 0;
}
