// wl_output — один выход.

#include "server.h"

#include <wayland-server-protocol.h>

namespace {
    void outputRelease(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct wl_output_interface outputImpl = {.release = outputRelease};

    void outputBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* server = (Server*)data;
        wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &outputImpl, server, nullptr);

        wl_output_send_geometry(res, 0, 0, 340, 210, WL_OUTPUT_SUBPIXEL_UNKNOWN, "imway",
                                "headless", WL_OUTPUT_TRANSFORM_NORMAL);
        wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, server->outW,
                            server->outH, (i32)(server->hz * 1000));

        if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
            wl_output_send_scale(res, 1);
        }

        if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
            wl_output_send_name(res, "HEADLESS-1");
        }

        if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
            wl_output_send_done(res);
        }
    }
}

void outputCreateGlobal(Server& server) {
    wl_global_create(server.display, &wl_output_interface, 4, &server, outputBind);
}
