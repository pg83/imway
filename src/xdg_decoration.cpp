// zxdg_decoration_manager_v1: всегда отвечаем server_side —
// декорация клиента = ImGui-окно композитора.

#include <xdg-decoration-unstable-v1-server-protocol.h>

#include "server.hpp"

namespace {

void res_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void deco_set_mode(wl_client*, wl_resource* res, uint32_t) {
    zxdg_toplevel_decoration_v1_send_configure(res, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void deco_unset_mode(wl_client*, wl_resource* res) {
    zxdg_toplevel_decoration_v1_send_configure(res, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

const struct zxdg_toplevel_decoration_v1_interface deco_impl = {
    .destroy = res_destroy,
    .set_mode = deco_set_mode,
    .unset_mode = deco_unset_mode,
};

void manager_get_toplevel_decoration(wl_client* client, wl_resource* res, uint32_t id,
                                     wl_resource* /*toplevel*/) {
    wl_resource* d = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface,
                                        wl_resource_get_version(res), id);
    if (!d) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(d, &deco_impl, nullptr, nullptr);
    zxdg_toplevel_decoration_v1_send_configure(d, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

const struct zxdg_decoration_manager_v1_interface manager_impl = {
    .destroy = res_destroy,
    .get_toplevel_decoration = manager_get_toplevel_decoration,
};

void manager_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res =
        wl_resource_create(client, &zxdg_decoration_manager_v1_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &manager_impl, data, nullptr);
}

} // namespace

void xdg_decoration_create_global(Server& server) {
    wl_global_create(server.display, &zxdg_decoration_manager_v1_interface, 1, &server,
                     manager_bind);
}
