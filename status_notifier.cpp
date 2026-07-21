#include "status_notifier.h"

#include "composer.h"
#include "dbus_conn.h"
#include "icon.h"
#include "icon_pool.h"
#include "scene.h"
#include "small_obj_allocator.h"
#include "util.h"

#include <dbus/dbus.h>

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr const char* kWatcher = "org.kde.StatusNotifierWatcher";
    constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
    constexpr const char* kItem = "org.kde.StatusNotifierItem";
    constexpr const char* kMenu = "com.canonical.dbusmenu";
    constexpr const char* kProps = "org.freedesktop.DBus.Properties";
    constexpr int kTimeout = 3000;
    constexpr int kMaxMenuDepth = 16;
    constexpr int kMaxMenuItems = 1024;

    struct StatusNotifierImpl;

    DBusHandlerResult watcherMessage(DBusConnection*, DBusMessage* msg, void* data);
    DBusHandlerResult busSignal(DBusConnection*, DBusMessage* msg, void* data);
    void propertiesReply(DBusPendingCall* pc, void* data);
    void menuReply(DBusPendingCall* pc, void* data);

    struct ItemBox: public StatusNotifierItem {
        StatusNotifierImpl* impl = nullptr;
        StringBuilder service;
        StringBuilder path;
        StringBuilder owner;
        StringBuilder menuPath;
        Vector<StatusMenuItem*> menuNodes;
        bool registered = false;
        bool itemIsMenu = false;
    };

    struct StatusNotifierImpl: public StatusNotifier {
        Composer* c = nullptr;
        DBusConnection* conn = nullptr;
        Vector<ItemBox*> all;
        bool ownsWatcher = false;

        StatusNotifierImpl(Composer& comp);
        ~StatusNotifierImpl() noexcept;

        void itemsImpl(VisitorFace&& vis) override;
        void activate(const StatusAction& action, int x, int y) override;

        ItemBox* find(StringView service, StringView path);
        ItemBox* findSignal(DBusMessage* msg);
        void registerItem(DBusMessage* msg);
        void unregisterOwner(StringView owner);
        void getProperties(ItemBox& item);
        void getMenu(ItemBox& item);
        void aboutToShow(ItemBox& item, i32 id);
        void clearMenu(ItemBox& item);
        void readProperties(ItemBox& item, DBusMessage* reply);
        void readMenu(ItemBox& item, DBusMessage* reply);
        void changed(ItemBox& item);
        bool sendReply(DBusMessage* msg, DBusPendingCallNotifyFunction cb, void* data);
        void sendSimple(ItemBox& item, const char* iface, const char* method, int x, int y);
        void sendMenuEvent(ItemBox& item, i32 id);
        void watcherGet(DBusMessage* msg);
        void watcherGetAll(DBusMessage* msg);
        void emitItem(const char* member, ItemBox& item);
    };

    StringView text(const StringBuilder& b) {
        return StringView((const Buffer&)b);
    }

    void assign(StringBuilder& out, StringView value) {
        out.reset();
        out << value;
    }

    StringView iterString(DBusMessageIter* it) {
        int type = dbus_message_iter_get_arg_type(it);

        if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH && type != DBUS_TYPE_SIGNATURE) {
            return {};
        }

        const char* value = "";

        dbus_message_iter_get_basic(it, &value);

        return StringView(value);
    }

    bool iterBool(DBusMessageIter* it, bool fallback = false) {
        if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_BOOLEAN) {
            return fallback;
        }

        dbus_bool_t value = FALSE;

        dbus_message_iter_get_basic(it, &value);

        return value;
    }

    i32 iterI32(DBusMessageIter* it, i32 fallback = 0) {
        if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_INT32) {
            return fallback;
        }

        i32 value = 0;

        dbus_message_iter_get_basic(it, &value);

        return value;
    }

    template <typename F>
    void eachDict(DBusMessageIter* array, F f) {
        if (dbus_message_iter_get_arg_type(array) != DBUS_TYPE_ARRAY) {
            return;
        }

        DBusMessageIter entry;

        dbus_message_iter_recurse(array, &entry);

        while (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter pair;

            dbus_message_iter_recurse(&entry, &pair);

            const char* key = "";

            if (dbus_message_iter_get_arg_type(&pair) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&pair, &key);
                dbus_message_iter_next(&pair);
            }

            if (dbus_message_iter_get_arg_type(&pair) == DBUS_TYPE_VARIANT) {
                DBusMessageIter value;

                dbus_message_iter_recurse(&pair, &value);
                f(StringView(key), &value);
            }

            dbus_message_iter_next(&entry);
        }
    }

    void appendBoolVariant(DBusMessageIter* dict, const char* key, bool value) {
        DBusMessageIter entry, var;
        dbus_bool_t b = value;

        dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(dict, &entry);
    }

    void appendIntVariant(DBusMessageIter* dict, const char* key, i32 value) {
        DBusMessageIter entry, var;

        dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &value);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(dict, &entry);
    }

    void readPixmap(StatusNotifierImpl& impl, Icon*& target, DBusMessageIter* value) {
        if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_ARRAY) {
            return;
        }

        int bestW = 0, bestH = 0;
        Vector<u8> best;
        DBusMessageIter images;

        dbus_message_iter_recurse(value, &images);

        while (dbus_message_iter_get_arg_type(&images) == DBUS_TYPE_STRUCT) {
            DBusMessageIter fields;

            dbus_message_iter_recurse(&images, &fields);
            i32 w = iterI32(&fields);
            dbus_message_iter_next(&fields);
            i32 h = iterI32(&fields);
            dbus_message_iter_next(&fields);

            i64 pixels = (i64)w * h;

            if (w > 0 && h > 0 && w <= 1024 && h <= 1024 && pixels > (i64)bestW * bestH &&
                dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
                DBusMessageIter bytes;

                dbus_message_iter_recurse(&fields, &bytes);
                Vector<u8> candidate;

                while (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE && candidate.length() < (size_t)pixels * 4) {
                    u8 byte = 0;

                    dbus_message_iter_get_basic(&bytes, &byte);
                    candidate.pushBack(byte);
                    dbus_message_iter_next(&bytes);
                }

                if (candidate.length() == (size_t)pixels * 4) {
                    best.clear();

                    for (u8 byte : candidate) {
                        best.pushBack(byte);
                    }

                    bestW = w;
                    bestH = h;
                }
            }

            dbus_message_iter_next(&images);
        }

        if (!bestW) {
            if (target) {
                impl.c->iconPool->release(target);
                target = nullptr;
            }

            return;
        }

        if (target) {
            impl.c->iconPool->release(target);
        }

        target = impl.c->iconPool->acquire();
        target->width = bestW;
        target->height = bestH;

        for (size_t p = 0; p < (size_t)bestW * bestH; p++) {
            // SNI transports pixels as network-order ARGB bytes.  Icon stores
            // host u32 ARGB and the renderer performs the BGRA upload mapping.
            u32 argb = ((u32)best[p * 4] << 24) | ((u32)best[p * 4 + 1] << 16) |
                ((u32)best[p * 4 + 2] << 8) | best[p * 4 + 3];

            target->argb.pushBack(argb);
        }
    }

    void readItemProperty(StatusNotifierImpl& impl, ItemBox& item, StringView key, DBusMessageIter* value) {
        if (key == "Id"_sv) {
            assign(item.id, iterString(value));
        } else if (key == "Title"_sv) {
            assign(item.title, iterString(value));
        } else if (key == "DesktopEntry"_sv) {
            StringView desktop = iterString(value);

            if (desktop.endsWith(".desktop"_sv)) {
                desktop = desktop.prefix(desktop.length() - 8);
            }

            assign(item.desktopEntry, desktop);
        } else if (key == "Status"_sv) {
            assign(item.status, iterString(value));
        } else if (key == "IconName"_sv) {
            assign(item.iconName, iterString(value));
        } else if (key == "AttentionIconName"_sv) {
            assign(item.attentionIconName, iterString(value));
        } else if (key == "Menu"_sv) {
            StringView path = iterString(value);

            if (text(item.menuPath) != path) {
                impl.clearMenu(item);
                assign(item.menuPath, path);
            }

            item.hasMenu = !path.empty();
        } else if (key == "ItemIsMenu"_sv) {
            item.itemIsMenu = iterBool(value);
        } else if (key == "IconPixmap"_sv) {
            readPixmap(impl, item.iconPixmap, value);
        } else if (key == "AttentionIconPixmap"_sv) {
            readPixmap(impl, item.attentionIconPixmap, value);
        }
    }

    StatusMenuItem* parseMenuNode(StatusNotifierImpl& impl, ItemBox& item, DBusMessageIter* node,
        StatusMenuItem* parent, int depth, int& count) {
        if (depth > kMaxMenuDepth || count >= kMaxMenuItems || dbus_message_iter_get_arg_type(node) != DBUS_TYPE_STRUCT) {
            return nullptr;
        }

        DBusMessageIter fields;

        dbus_message_iter_recurse(node, &fields);

        if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_INT32) {
            return nullptr;
        }

        i32 id = iterI32(&fields);
        dbus_message_iter_next(&fields);

        StatusMenuItem* entry = nullptr;

        if (id != 0) {
            entry = impl.c->alloc->make<StatusMenuItem>();
            entry->action = {&item, StatusActionKind::menu, id};
            entry->open = {&item, StatusActionKind::menuOpen, id};
            item.menuNodes.pushBack(entry);
            count++;
        }

        if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
            eachDict(&fields, [&](StringView key, DBusMessageIter* value) {
                if (!entry) {
                    return;
                }

                if (key == "label"_sv) {
                    assign(entry->label, iterString(value));
                } else if (key == "enabled"_sv) {
                    entry->enabled = iterBool(value, true);
                } else if (key == "visible"_sv) {
                    entry->visible = iterBool(value, true);
                } else if (key == "type"_sv) {
                    entry->separator = iterString(value) == "separator"_sv;
                } else if (key == "toggle-type"_sv) {
                    entry->checkable = !iterString(value).empty();
                } else if (key == "toggle-state"_sv) {
                    entry->checked = iterI32(value) > 0;
                }
            });

            dbus_message_iter_next(&fields);
        }

        StatusMenuItem* childParent = entry ? entry : parent;

        if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
            DBusMessageIter children;

            dbus_message_iter_recurse(&fields, &children);

            while (dbus_message_iter_get_arg_type(&children) == DBUS_TYPE_VARIANT) {
                DBusMessageIter child;

                dbus_message_iter_recurse(&children, &child);
                StatusMenuItem* parsed = parseMenuNode(impl, item, &child, childParent, depth + 1, count);

                if (parsed) {
                    if (childParent) {
                        childParent->children.pushBack(parsed);
                    } else {
                        item.menu.pushBack(parsed);
                    }
                }

                dbus_message_iter_next(&children);
            }
        }

        return entry;
    }

    DBusMessage* steal(DBusPendingCall* pc) {
        DBusMessage* reply = dbus_pending_call_steal_reply(pc);

        dbus_pending_call_unref(pc);

        if (reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
            dbus_message_unref(reply);

            return nullptr;
        }

        return reply;
    }
}

StatusNotifierImpl::StatusNotifierImpl(Composer& comp)
    : c(&comp)
    , conn(comp.bus->raw())
{
    DBusError err;

    dbus_error_init(&err);
    int rc = dbus_bus_request_name(conn, kWatcher, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);

    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        sysE << "imway: StatusNotifierWatcher is taken ("_sv << rc << "), dock tray disabled"_sv << endL;
        dbus_error_free(&err);

        return;
    }

    ownsWatcher = true;
    DBusObjectPathVTable vt{};

    vt.message_function = watcherMessage;
    dbus_connection_register_object_path(conn, kWatcherPath, &vt, this);
    dbus_bus_add_match(conn, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'", nullptr);
    dbus_bus_add_match(conn, "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'", nullptr);
    dbus_bus_add_match(conn, "type='signal',interface='org.kde.StatusNotifierItem'", nullptr);
    dbus_bus_add_match(conn, "type='signal',interface='com.canonical.dbusmenu',member='LayoutUpdated'", nullptr);
    dbus_connection_add_filter(conn, busSignal, this, nullptr);
    sysO << "imway: StatusNotifierWatcher on the session bus"_sv << endL;
}

StatusNotifierImpl::~StatusNotifierImpl() noexcept {
    for (ItemBox* item : all) {
        if (item->iconPixmap) {
            c->iconPool->release(item->iconPixmap);
        }

        if (item->attentionIconPixmap) {
            c->iconPool->release(item->attentionIconPixmap);
        }
    }
}

void StatusNotifierImpl::itemsImpl(VisitorFace&& vis) {
    if (!ownsWatcher) {
        return;
    }

    for (ItemBox* item : all) {
        if (item->registered) {
            vis.visit(item);
        }
    }
}

ItemBox* StatusNotifierImpl::find(StringView service, StringView path) {
    for (ItemBox* item : all) {
        if (text(item->service) == service && text(item->path) == path) {
            return item;
        }
    }

    return nullptr;
}

    ItemBox* StatusNotifierImpl::findSignal(DBusMessage* msg) {
    StringView sender(dbus_message_get_sender(msg) ? dbus_message_get_sender(msg) : "");
    StringView path(dbus_message_get_path(msg) ? dbus_message_get_path(msg) : "");

    for (ItemBox* item : all) {
        if (item->registered && (text(item->path) == path || text(item->menuPath) == path) &&
            (text(item->owner) == sender || text(item->service) == sender)) {
            return item;
        }
    }

    return nullptr;
}

void StatusNotifierImpl::changed(ItemBox& item) {
    c->scene->needsFrame = true;

    if (!item.menuPath.empty() && item.menu.empty()) {
        getMenu(item);
    }
}

bool StatusNotifierImpl::sendReply(DBusMessage* msg, DBusPendingCallNotifyFunction cb, void* data) {
    DBusPendingCall* pc = nullptr;

    if (!msg || !dbus_connection_send_with_reply(conn, msg, &pc, kTimeout) || !pc) {
        if (msg) {
            dbus_message_unref(msg);
        }

        return false;
    }

    dbus_pending_call_set_notify(pc, cb, data, nullptr);
    dbus_message_unref(msg);

    return true;
}

void StatusNotifierImpl::getProperties(ItemBox& item) {
    const char* iface = kItem;
    Buffer service(text(item.service)), path(text(item.path));
    DBusMessage* msg = dbus_message_new_method_call(service.cStr(), path.cStr(), kProps, "GetAll");

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    sendReply(msg, propertiesReply, &item);
}

void StatusNotifierImpl::getMenu(ItemBox& item) {
    if (!item.registered || item.menuPath.empty()) {
        return;
    }

    Buffer service(text(item.service)), path(text(item.menuPath));
    DBusMessage* msg = dbus_message_new_method_call(service.cStr(), path.cStr(), kMenu, "GetLayout");
    i32 parent = 0, depth = -1;
    DBusMessageIter it, names;

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &parent);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &depth);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &names);
    dbus_message_iter_close_container(&it, &names);
    sendReply(msg, menuReply, &item);
}

void StatusNotifierImpl::aboutToShow(ItemBox& item, i32 id) {
    if (item.menuPath.empty()) {
        return;
    }

    Buffer service(text(item.service)), path(text(item.menuPath));
    DBusMessage* msg = dbus_message_new_method_call(service.cStr(), path.cStr(), kMenu, "AboutToShow");

    dbus_message_append_args(msg, DBUS_TYPE_INT32, &id, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
    getMenu(item);
}

void StatusNotifierImpl::readProperties(ItemBox& item, DBusMessage* reply) {
    DBusMessageIter it;

    if (!reply || !dbus_message_iter_init(reply, &it)) {
        return;
    }

    eachDict(&it, [&](StringView key, DBusMessageIter* value) {
        readItemProperty(*this, item, key, value);
    });
    changed(item);
}

void StatusNotifierImpl::clearMenu(ItemBox& item) {
    item.menu.clear();

    for (StatusMenuItem* entry : item.menuNodes) {
        c->alloc->release(entry);
    }

    item.menuNodes.clear();
}

void StatusNotifierImpl::readMenu(ItemBox& item, DBusMessage* reply) {
    DBusMessageIter it;

    if (!reply || !dbus_message_iter_init(reply, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_UINT32 ||
        !dbus_message_iter_next(&it)) {
        return;
    }

    clearMenu(item);
    int count = 0;

    parseMenuNode(*this, item, &it, nullptr, 0, count);
    c->scene->needsFrame = true;
}

void StatusNotifierImpl::emitItem(const char* member, ItemBox& item) {
    DBusMessage* sig = dbus_message_new_signal(kWatcherPath, kWatcher, member);
    StringBuilder value;

    value << text(item.service) << text(item.path);
    const char* p = value.cStr();

    dbus_message_append_args(sig, DBUS_TYPE_STRING, &p, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
}

void StatusNotifierImpl::registerItem(DBusMessage* msg) {
    const char* arg = "";

    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID) || !arg[0]) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "expected service name or object path");

        dbus_connection_send(conn, err, nullptr);
        dbus_message_unref(err);

        return;
    }

    StringView sender(dbus_message_get_sender(msg) ? dbus_message_get_sender(msg) : "");
    StringView service = arg[0] == '/' ? sender : StringView(arg);
    StringView path = arg[0] == '/' ? StringView(arg) : "/StatusNotifierItem"_sv;
    ItemBox* item = find(service, path);

    if (!item) {
        item = c->alloc->make<ItemBox>();
        item->impl = this;
        assign(item->service, service);
        assign(item->path, path);
        item->primary = {item, StatusActionKind::primary, 0};
        item->context = {item, StatusActionKind::context, 0};
        all.pushBack(item);
    }

    assign(item->owner, sender);
    bool newlyRegistered = !item->registered;

    item->registered = true;
    getProperties(*item);

    if (newlyRegistered) {
        emitItem("StatusNotifierItemRegistered", *item);
    }

    DBusMessage* reply = dbus_message_new_method_return(msg);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    c->scene->needsFrame = true;
}

void StatusNotifierImpl::unregisterOwner(StringView owner) {
    for (ItemBox* item : all) {
        if (item->registered && text(item->owner) == owner) {
            item->registered = false;
            emitItem("StatusNotifierItemUnregistered", *item);
            clearMenu(*item);
        }
    }

    c->scene->needsFrame = true;
}

void StatusNotifierImpl::sendSimple(ItemBox& item, const char* iface, const char* method, int x, int y) {
    Buffer service(text(item.service)), path(text(item.path));
    DBusMessage* msg = dbus_message_new_method_call(service.cStr(), path.cStr(), iface, method);
    i32 px = x, py = y;

    dbus_message_append_args(msg, DBUS_TYPE_INT32, &px, DBUS_TYPE_INT32, &py, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void StatusNotifierImpl::sendMenuEvent(ItemBox& item, i32 id) {
    if (item.menuPath.empty()) {
        return;
    }

    Buffer service(text(item.service)), path(text(item.menuPath));
    DBusMessage* msg = dbus_message_new_method_call(service.cStr(), path.cStr(), kMenu, "Event");
    DBusMessageIter it, var;
    const char* event = "clicked";
    const char* empty = "";
    u32 stamp = nowMsec();

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &id);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &event);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &empty);
    dbus_message_iter_close_container(&it, &var);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &stamp);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void StatusNotifierImpl::activate(const StatusAction& action, int x, int y) {
    auto* item = (ItemBox*)action.item;

    if (!item || !item->registered || item->impl != this) {
        return;
    }

    switch (action.kind) {
    case StatusActionKind::primary:
        sendSimple(*item, kItem, item->itemIsMenu ? "ContextMenu" : "Activate", x, y);
        break;
    case StatusActionKind::context:
        if (!item->menuPath.empty()) {
            aboutToShow(*item, 0);
        } else {
            sendSimple(*item, kItem, "ContextMenu", x, y);
        }
        break;
    case StatusActionKind::menuOpen:
        aboutToShow(*item, action.menuId);
        break;
    case StatusActionKind::menu:
        sendMenuEvent(*item, action.menuId);
        break;
    }
}

void StatusNotifierImpl::watcherGet(DBusMessage* msg) {
    const char* iface = "", *property = "";

    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID) ||
        StringView(iface) != kWatcher) {
        return;
    }

    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it, var, arr;

    dbus_message_iter_init_append(reply, &it);

    if (StringView(property) == "ProtocolVersion"_sv) {
        i32 version = 0;

        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "i", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &version);
        dbus_message_iter_close_container(&it, &var);
    } else if (StringView(property) == "IsStatusNotifierHostRegistered"_sv) {
        dbus_bool_t yes = TRUE;

        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &yes);
        dbus_message_iter_close_container(&it, &var);
    } else if (StringView(property) == "RegisteredStatusNotifierItems"_sv) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "as", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);

        for (ItemBox* item : all) {
            if (item->registered) {
                StringBuilder value;

                value << text(item->service) << text(item->path);
                const char* p = value.cStr();

                dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &p);
            }
        }

        dbus_message_iter_close_container(&var, &arr);
        dbus_message_iter_close_container(&it, &var);
    } else {
        dbus_message_unref(reply);

        return;
    }

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void StatusNotifierImpl::watcherGetAll(DBusMessage* msg) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it, dict, entry, var, arr;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    appendIntVariant(&dict, "ProtocolVersion", 0);
    appendBoolVariant(&dict, "IsStatusNotifierHostRegistered", true);

    const char* key = "RegisteredStatusNotifierItems";

    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);

    for (ItemBox* item : all) {
        if (item->registered) {
            StringBuilder value;

            value << text(item->service) << text(item->path);
            const char* p = value.cStr();

            dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &p);
        }
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

namespace {
    void propertiesReply(DBusPendingCall* pc, void* data) {
        auto* item = (ItemBox*)data;
        DBusMessage* reply = steal(pc);

        if (reply && item->registered) {
            item->impl->readProperties(*item, reply);
        }

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    void menuReply(DBusPendingCall* pc, void* data) {
        auto* item = (ItemBox*)data;
        DBusMessage* reply = steal(pc);

        if (reply && item->registered) {
            item->impl->readMenu(*item, reply);
        }

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    DBusHandlerResult watcherMessage(DBusConnection*, DBusMessage* msg, void* data) {
        auto* impl = (StatusNotifierImpl*)data;

        if (dbus_message_is_method_call(msg, kWatcher, "RegisterStatusNotifierItem")) {
            impl->registerItem(msg);
        } else if (dbus_message_is_method_call(msg, kWatcher, "RegisterStatusNotifierHost")) {
            DBusMessage* reply = dbus_message_new_method_return(msg);

            dbus_connection_send(impl->conn, reply, nullptr);
            dbus_message_unref(reply);
        } else if (dbus_message_is_method_call(msg, kProps, "Get")) {
            impl->watcherGet(msg);
        } else if (dbus_message_is_method_call(msg, kProps, "GetAll")) {
            impl->watcherGetAll(msg);
        } else {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    DBusHandlerResult busSignal(DBusConnection*, DBusMessage* msg, void* data) {
        auto* impl = (StatusNotifierImpl*)data;

        if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
            const char *name = "", *oldOwner = "", *newOwner = "";

            if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &oldOwner,
                    DBUS_TYPE_STRING, &newOwner, DBUS_TYPE_INVALID) && oldOwner[0] && !newOwner[0]) {
                impl->unregisterOwner(StringView(name));
            }

            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        ItemBox* item = impl->findSignal(msg);

        if (!item) {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        if (dbus_message_is_signal(msg, kProps, "PropertiesChanged")) {
            DBusMessageIter it;

            if (dbus_message_iter_init(msg, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING &&
                iterString(&it) == kItem && dbus_message_iter_next(&it)) {
                eachDict(&it, [&](StringView key, DBusMessageIter* value) {
                    readItemProperty(*impl, *item, key, value);
                });
                impl->changed(*item);

                if (dbus_message_iter_next(&it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
                    DBusMessageIter invalidated;

                    dbus_message_iter_recurse(&it, &invalidated);

                    if (dbus_message_iter_get_arg_type(&invalidated) == DBUS_TYPE_STRING) {
                        impl->getProperties(*item);
                    }
                }
            }
        } else if (StringView(dbus_message_get_interface(msg) ? dbus_message_get_interface(msg) : "") == kItem) {
            // The original SNI protocol predates PropertiesChanged and many
            // implementations still emit NewIcon/NewStatus/NewTitle only.
            impl->getProperties(*item);
        } else if (dbus_message_is_signal(msg, kMenu, "LayoutUpdated")) {
            impl->getMenu(*item);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

StatusNotifier* StatusNotifier::create(Composer& c) {
    return c.pool->make<StatusNotifierImpl>(c);
}
