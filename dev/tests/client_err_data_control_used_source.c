#include "wl_util.h"

#include <ext-data-control-v1-client-protocol.h>

static struct ext_data_control_manager_v1* mgr;
static struct wl_seat* seat2;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
        mgr = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat2)
        seat2 = wl_registry_bind(registry, name, &wl_seat_interface, 5);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !seat2) return 2;

    struct ext_data_control_device_v1* dev =
        ext_data_control_manager_v1_get_data_device(mgr, seat2);
    struct ext_data_control_source_v1* src =
        ext_data_control_manager_v1_create_data_source(mgr);

    ext_data_control_source_v1_offer(src, "text/plain");
    ext_data_control_device_v1_set_selection(dev, src);
    ext_data_control_device_v1_set_primary_selection(dev, src);
    return wl_expect_error(ext_data_control_device_v1_interface.name,
                           EXT_DATA_CONTROL_DEVICE_V1_ERROR_USED_SOURCE);
}
