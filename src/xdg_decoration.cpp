// zxdg_decoration_manager_v1: всегда отвечаем server_side —
// декорация клиента = ImGui-окно композитора.

#include "server.h"

#include <xdg-decoration-unstable-v1-server-protocol.h>

namespace {
    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void decoSetMode(wl_client*, wl_resource* res, u32) {
        zxdg_toplevel_decoration_v1_send_configure(res,
                                                   ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    void decoUnsetMode(wl_client*, wl_resource* res) {
        zxdg_toplevel_decoration_v1_send_configure(res,
                                                   ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    const struct zxdg_toplevel_decoration_v1_interface decoImpl = {
        .destroy = resDestroy,
        .set_mode = decoSetMode,
        .unset_mode = decoUnsetMode,
    };

    void managerGetToplevelDecoration(wl_client* client, wl_resource* res, u32 id,
                                      wl_resource* /*toplevel*/) {
        wl_resource* d = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface,
                                            wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(d, &decoImpl, nullptr, nullptr);
        zxdg_toplevel_decoration_v1_send_configure(d, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    const struct zxdg_decoration_manager_v1_interface managerImpl = {
        .destroy = resDestroy,
        .get_toplevel_decoration = managerGetToplevelDecoration,
    };

    void managerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res =
            wl_resource_create(client, &zxdg_decoration_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &managerImpl, data, nullptr);
    }
}

void xdgDecorationCreateGlobal(Server& server) {
    wl_global_create(server.display, &zxdg_decoration_manager_v1_interface, 1, &server,
                     managerBind);
}
