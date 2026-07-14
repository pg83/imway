// wl_data_device_manager — инертная заглушка (реальный clipboard/DnD впереди).

#include "server.h"

#include <wayland-server-protocol.h>

namespace {
    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    // --- wl_data_source ---

    void sourceOffer(wl_client*, wl_resource*, const char*) {
    }

    void sourceSetActions(wl_client*, wl_resource*, u32) {
    }

    const struct wl_data_source_interface sourceImpl = {
        .offer = sourceOffer,
        .destroy = resDestroy,
        .set_actions = sourceSetActions,
    };

    // --- wl_data_device ---

    void deviceStartDrag(wl_client*, wl_resource*, wl_resource*, wl_resource*, wl_resource*,
                         u32) {
    }

    void deviceSetSelection(wl_client*, wl_resource*, wl_resource*, u32) {
    }

    const struct wl_data_device_interface deviceImpl = {
        .start_drag = deviceStartDrag,
        .set_selection = deviceSetSelection,
        .release = resDestroy,
    };

    // --- wl_data_device_manager ---

    void managerCreateDataSource(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* s = wl_resource_create(client, &wl_data_source_interface,
                                            wl_resource_get_version(res), id);

        if (s) {
            wl_resource_set_implementation(s, &sourceImpl, nullptr, nullptr);
        }
    }

    void managerGetDataDevice(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        wl_resource* d = wl_resource_create(client, &wl_data_device_interface,
                                            wl_resource_get_version(res), id);

        if (d) {
            wl_resource_set_implementation(d, &deviceImpl, nullptr, nullptr);
        }
    }

    const struct wl_data_device_manager_interface managerImpl = {
        .create_data_source = managerCreateDataSource,
        .get_data_device = managerGetDataDevice,
    };

    void managerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res =
            wl_resource_create(client, &wl_data_device_manager_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &managerImpl, data, nullptr);
    }
}

void dataDeviceCreateGlobal(Server& server) {
    wl_global_create(server.display, &wl_data_device_manager_interface, 3, &server, managerBind);
}
