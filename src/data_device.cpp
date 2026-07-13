// wl_data_device_manager — инертная заглушка (реальный clipboard/DnD — M3).

#include <wayland-server-protocol.h>

#include "server.hpp"

namespace {

void res_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

// --- wl_data_source ---
void source_offer(wl_client*, wl_resource*, const char*) {}
void source_set_actions(wl_client*, wl_resource*, uint32_t) {}

const struct wl_data_source_interface source_impl = {
    .offer = source_offer,
    .destroy = res_destroy,
    .set_actions = source_set_actions,
};

// --- wl_data_device ---
void device_start_drag(wl_client*, wl_resource*, wl_resource*, wl_resource*, wl_resource*,
                       uint32_t) {}
void device_set_selection(wl_client*, wl_resource*, wl_resource*, uint32_t) {}

const struct wl_data_device_interface device_impl = {
    .start_drag = device_start_drag,
    .set_selection = device_set_selection,
    .release = res_destroy,
};

// --- wl_data_device_manager ---
void manager_create_data_source(wl_client* client, wl_resource* res, uint32_t id) {
    wl_resource* s =
        wl_resource_create(client, &wl_data_source_interface, wl_resource_get_version(res), id);
    if (s) wl_resource_set_implementation(s, &source_impl, nullptr, nullptr);
}

void manager_get_data_device(wl_client* client, wl_resource* res, uint32_t id, wl_resource*) {
    wl_resource* d =
        wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(res), id);
    if (d) wl_resource_set_implementation(d, &device_impl, nullptr, nullptr);
}

const struct wl_data_device_manager_interface manager_impl = {
    .create_data_source = manager_create_data_source,
    .get_data_device = manager_get_data_device,
};

void manager_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res =
        wl_resource_create(client, &wl_data_device_manager_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &manager_impl, data, nullptr);
}

} // namespace

void data_device_create_global(Server& server) {
    wl_global_create(server.display, &wl_data_device_manager_interface, 3, &server, manager_bind);
}
