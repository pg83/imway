// presentation-time on a KMS output: once the display has flipped, a
// presented frame carries the kernel's page-flip timestamp and vblank
// sequence and claims vsync, hardware clock and hardware completion; the
// timestamp is on the monotonic clock the compositor announced.

#include "wl_util.h"
#include <presentation-time-client-protocol.h>

#include <time.h>

static struct wp_presentation* presentation;
static struct wl_toplevel_ctx top;
static uint32_t clock_id = ~0u;
static int presented, hardware;
static uint32_t seq_lo;
static uint64_t stamp_ns;

static void pres_clock_id(void* d, struct wp_presentation* p, uint32_t id) {
    (void)d; (void)p;
    clock_id = id;
}
static const struct wp_presentation_listener pres_listener = {pres_clock_id};

static void fb_sync_output(void* d, struct wp_presentation_feedback* f, struct wl_output* o) {
    (void)d; (void)f; (void)o;
}
static void fb_presented(void* d, struct wp_presentation_feedback* f, uint32_t th, uint32_t tl,
                         uint32_t tn, uint32_t refresh, uint32_t sh, uint32_t sl, uint32_t flags) {
    (void)d; (void)refresh; (void)sh;
    uint32_t want = WP_PRESENTATION_FEEDBACK_KIND_VSYNC | WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK | WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;

    presented++;
    if ((flags & want) == want) {
        hardware = 1;
        seq_lo = sl;
        stamp_ns = (((uint64_t)th << 32) | tl) * 1000000000ull + tn;
    }
    wp_presentation_feedback_destroy(f);
}
static void fb_discarded(void* d, struct wp_presentation_feedback* f) {
    (void)d;
    wp_presentation_feedback_destroy(f);
}
static const struct wp_presentation_feedback_listener fb_listener = {
    fb_sync_output, fb_presented, fb_discarded,
};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_presentation_interface.name))
        presentation = wl_registry_bind(r, name, &wp_presentation_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!presentation) { fprintf(stderr, "no wp_presentation\n"); return 1; }
    wp_presentation_add_listener(presentation, &pres_listener, NULL);

    wl_make_toplevel(&top, "client_kms_presentation", 300, 200, 0xFFFF0000);

    for (int i = 0; i < 400 && !hardware; i++) {
        struct wp_presentation_feedback* fb = wp_presentation_feedback(presentation, top.surface);
        wp_presentation_feedback_add_listener(fb, &fb_listener, NULL);
        wl_surface_attach(top.surface, wl_solid(300, 200, 0xFFFF0000), 0, 0);
        wl_surface_damage(top.surface, 0, 0, 300, 200);
        wl_surface_commit(top.surface);
        wl_display_roundtrip(wl_dpy);
        usleep(30000);
    }

    if (clock_id != CLOCK_MONOTONIC) { fprintf(stderr, "clock_id %u, not monotonic\n", clock_id); return 1; }
    if (!hardware) { fprintf(stderr, "%d presented, none with hardware timing\n", presented); return 1; }
    if (!seq_lo) { fprintf(stderr, "no vblank sequence\n"); return 1; }

    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;

    if (stamp_ns > now_ns || now_ns - stamp_ns > 10000000000ull) {
        fprintf(stderr, "flip timestamp %llu is not recent monotonic time (now %llu)\n",
                (unsigned long long)stamp_ns, (unsigned long long)now_ns);
        return 1;
    }

    printf("client_kms_presentation: hardware presented, seq %u\n", seq_lo);
    return 0;
}
