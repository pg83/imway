// wl_output — один headless-выход. (Seat переехал в seat.cpp.)

#include <wayland-server-protocol.h>

#include "server.hpp"

namespace {

void output_release(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

const struct wl_output_interface output_impl = {.release = output_release};

void output_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* server = (Server*)data;
    wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &output_impl, server, nullptr);

    wl_output_send_geometry(res, 0, 0, 340, 210, WL_OUTPUT_SUBPIXEL_UNKNOWN, "imway",
                            "headless", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, server->out_w,
                        server->out_h, (int32_t)(server->hz * 1000));
    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) wl_output_send_scale(res, 1);
    if (version >= WL_OUTPUT_NAME_SINCE_VERSION) wl_output_send_name(res, "HEADLESS-1");
    if (version >= WL_OUTPUT_DONE_SINCE_VERSION) wl_output_send_done(res);
}

} // namespace

void output_create_global(Server& server) {
    wl_global_create(server.display, &wl_output_interface, 4, &server, output_bind);
}
