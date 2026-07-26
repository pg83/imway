#include "dbus_menu.h"

#include "log.h"
#include "icon.h"
#include "util.h"
#include "scene.h"
#include "composer.h"
#include "dbus_conn.h"
#include "icon_pool.h"
#include "intr_list.h"
#include "small_obj_allocator.h"

#include <std/ios/sys.h>
#include <std/sym/i_map.h>
#include <std/mem/obj_pool.h>

#include <png.h>
#include <dbus/dbus.h>

using namespace stl;

namespace {
    constexpr const char* kMenuInterface = "com.canonical.dbusmenu";
    constexpr const char* kRegistrar = "com.canonical.AppMenu.Registrar";
    constexpr const char* kRegistrarPath = "/com/canonical/AppMenu/Registrar";
    constexpr int kTimeout = 3000;
    constexpr int kMaxDepth = 16;
    constexpr int kMaxItems = 4096;
    constexpr size_t kMaxIconBytes = 4 * 1024 * 1024;

    struct MenusImpl;
    struct MenuImpl;

    enum class CallKind {
        owner,
        layout,
        aboutToShow,
    };

    struct Pending {
        MenuImpl* menu = nullptr;
        DBusPendingCall* call = nullptr;
        CallKind kind = CallKind::layout;
        u64 sequence = 0;
    };

    struct MenuImpl: DBusMenu, IntrusiveNode {
        MenusImpl* parent = nullptr;
        ObjPool* pool = nullptr;
        ObjPool* modelPool = nullptr;
        IntMap<DBusMenuItem*>* byId = nullptr;
        StringBuilder service;
        StringBuilder path;
        StringBuilder owner;
        Vector<Pending*> pending;
        u64 nextSequence = 0;
        u64 newestLayout = 0;

        MenuImpl(MenusImpl& p, ObjPool* own, StringView serviceName, StringView objectPath);
        ~MenuImpl() noexcept;

        void prepare(i32 id) override;
        void activate(i32 id) override;
        void resolveOwner();
        void refresh();
        void reply(Pending& p);
        void readLayout(DBusMessage* reply, u64 sequence);
        void clearModel();
        void propertiesUpdated(DBusMessage* msg);
        void ownerChanged(StringView next);
        DBusMenuItem* find(i32 id);
    };

    struct Registration {
        u32 window = 0;
        StringBuilder sender;
        StringBuilder service;
        StringBuilder path;
    };

    struct MenusImpl: DBusMenus {
        Composer* composer = nullptr;
        DBusConnection* conn = nullptr;
        IntrusiveList menus;
        Vector<Registration*> registrations;
        bool ownsRegistrar = false;

        MenusImpl(Composer& c);
        ~MenusImpl() noexcept;

        DBusMenu* connect(StringView service, StringView path) override;
        void disconnect(DBusMenu* menu) override;
        bool send(MenuImpl& menu, DBusMessage* msg, CallKind kind, u64 sequence);
        void registrarMessage(DBusMessage* msg);
        void nameOwnerChanged(DBusMessage* msg);
        void signal(DBusMessage* msg);
        Registration* registration(u32 window);
        void eraseRegistration(size_t index, bool emit);
        void emitRegistered(const char* member, const Registration& reg);
    };

    StringView text(const StringBuilder& value) {
        return StringView((const Buffer&)value);
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

    u32 iterU32(DBusMessageIter* it, u32 fallback = 0) {
        if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_UINT32) {
            return fallback;
        }

        u32 value = 0;

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
            StringView key = iterString(&pair);

            if (!key.empty() && dbus_message_iter_next(&pair) && dbus_message_iter_get_arg_type(&pair) == DBUS_TYPE_VARIANT) {
                DBusMessageIter value;

                dbus_message_iter_recurse(&pair, &value);
                f(key, &value);
            }

            dbus_message_iter_next(&entry);
        }
    }

    void readLabel(StringBuilder& out, StringView raw) {
        out.reset();

        for (size_t i = 0; i < raw.length(); i++) {
            if (raw[i] != '_') {
                out << StringView(raw.begin() + i, 1);

                continue;
            }

            if (i + 1 < raw.length() && raw[i + 1] == '_') {
                out << "_"_sv;
                i++;
            }
        }
    }

    void readShortcut(StringBuilder& out, DBusMessageIter* value) {
        out.reset();

        if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_ARRAY) {
            return;
        }

        DBusMessageIter alternatives;

        dbus_message_iter_recurse(value, &alternatives);

        if (dbus_message_iter_get_arg_type(&alternatives) != DBUS_TYPE_ARRAY) {
            return;
        }

        DBusMessageIter parts;

        dbus_message_iter_recurse(&alternatives, &parts);
        bool first = true;

        while (dbus_message_iter_get_arg_type(&parts) == DBUS_TYPE_STRING) {
            StringView part = iterString(&parts);

            if (!first) {
                out << "+"_sv;
            }

            if (part == "Control"_sv) {
                out << "Ctrl"_sv;
            } else {
                out << part;
            }

            first = false;
            dbus_message_iter_next(&parts);
        }
    }

    Icon* readIcon(MenuImpl& menu, DBusMessageIter* value) {
        if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_ARRAY) {
            return nullptr;
        }

        DBusMessageIter bytes;

        dbus_message_iter_recurse(value, &bytes);

        if (dbus_message_iter_get_arg_type(&bytes) != DBUS_TYPE_BYTE) {
            return nullptr;
        }

        const u8* data = nullptr;
        int count = 0;

        dbus_message_iter_get_fixed_array(&bytes, &data, &count);

        if (!data || count <= 0 || (size_t)count > kMaxIconBytes) {
            return nullptr;
        }

        png_image image{};

        image.version = PNG_IMAGE_VERSION;

        if (!png_image_begin_read_from_memory(&image, data, (size_t)count) || !image.width || !image.height || image.width > 1024 || image.height > 1024) {
            png_image_free(&image);

            return nullptr;
        }

        image.format = PNG_FORMAT_RGBA;
        size_t pixels = (size_t)image.width * image.height;
        Vector<u8> rgba;

        rgba.zero(pixels * 4);

        if (!png_image_finish_read(&image, nullptr, rgba.mutData(), 0, nullptr)) {
            png_image_free(&image);

            return nullptr;
        }

        Icon* icon = menu.parent->composer->iconPool->acquire(*menu.modelPool);

        icon->width = (int)image.width;
        icon->height = (int)image.height;

        for (size_t i = 0; i < pixels; i++) {
            u32 a = rgba[i * 4 + 3];
            u32 r = (rgba[i * 4] * a + 127) / 255;
            u32 g = (rgba[i * 4 + 1] * a + 127) / 255;
            u32 b = (rgba[i * 4 + 2] * a + 127) / 255;

            icon->argb.pushBack((a << 24) | (r << 16) | (g << 8) | b);
        }

        png_image_free(&image);

        return icon;
    }

    void resetProperty(DBusMenuItem& item, StringView key) {
        if (key == "label"_sv) {
            item.label.reset();
        } else if (key == "enabled"_sv) {
            item.enabled = true;
        } else if (key == "visible"_sv) {
            item.visible = true;
        } else if (key == "type"_sv) {
            item.separator = false;
        } else if (key == "toggle-type"_sv) {
            item.toggle = DBusMenuToggle::none;
        } else if (key == "toggle-state"_sv) {
            item.toggleState = -1;
        } else if (key == "children-display"_sv) {
            item.submenu = false;
        } else if (key == "disposition"_sv) {
            item.disposition = DBusMenuDisposition::normal;
        } else if (key == "shortcut"_sv) {
            item.shortcut.reset();
        } else if (key == "icon-name"_sv) {
            item.iconName.reset();
        } else if (key == "icon-data"_sv) {
            item.iconData = nullptr;
        }
    }

    void readProperty(MenuImpl& menu, DBusMenuItem& item, StringView key, DBusMessageIter* value) {
        if (key == "label"_sv) {
            readLabel(item.label, iterString(value));
        } else if (key == "enabled"_sv) {
            item.enabled = iterBool(value, true);
        } else if (key == "visible"_sv) {
            item.visible = iterBool(value, true);
        } else if (key == "type"_sv) {
            item.separator = iterString(value) == "separator"_sv;
        } else if (key == "toggle-type"_sv) {
            StringView type = iterString(value);

            item.toggle = type == "checkmark"_sv ? DBusMenuToggle::checkmark : type == "radio"_sv ? DBusMenuToggle::radio : DBusMenuToggle::none;
        } else if (key == "toggle-state"_sv) {
            item.toggleState = iterI32(value, -1);
        } else if (key == "children-display"_sv) {
            item.submenu = iterString(value) == "submenu"_sv;
        } else if (key == "disposition"_sv) {
            StringView disposition = iterString(value);

            item.disposition = disposition == "informative"_sv ? DBusMenuDisposition::informative : disposition == "warning"_sv ? DBusMenuDisposition::warning : disposition == "alert"_sv ? DBusMenuDisposition::alert : DBusMenuDisposition::normal;
        } else if (key == "shortcut"_sv) {
            readShortcut(item.shortcut, value);
        } else if (key == "icon-name"_sv) {
            assign(item.iconName, iterString(value));
        } else if (key == "icon-data"_sv) {
            item.iconData = readIcon(menu, value);
        }
    }

    DBusMenuItem* readNode(MenuImpl& menu, DBusMessageIter* node, Vector<DBusMenuItem*>& destination, int depth, int& count) {
        if (depth > kMaxDepth || count >= kMaxItems || dbus_message_iter_get_arg_type(node) != DBUS_TYPE_STRUCT) {
            return nullptr;
        }

        DBusMessageIter fields;

        dbus_message_iter_recurse(node, &fields);

        if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_INT32) {
            return nullptr;
        }

        i32 id = iterI32(&fields);
        dbus_message_iter_next(&fields);
        DBusMenuItem* item = nullptr;

        if (id != 0) {
            item = menu.modelPool->make<DBusMenuItem>();
            item->id = id;
            count++;
        }

        if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
            eachDict(&fields, [&](StringView key, DBusMessageIter* value) {
                if (item) {
                    readProperty(menu, *item, key, value);
                }
            });
            dbus_message_iter_next(&fields);
        }

        Vector<DBusMenuItem*>* children = item ? &item->children : &destination;

        if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
            DBusMessageIter values;

            dbus_message_iter_recurse(&fields, &values);

            while (dbus_message_iter_get_arg_type(&values) == DBUS_TYPE_VARIANT) {
                DBusMessageIter child;

                dbus_message_iter_recurse(&values, &child);
                readNode(menu, &child, *children, depth + 1, count);
                dbus_message_iter_next(&values);
            }
        }

        if (item) {
            item->submenu = item->submenu || !item->children.empty();
            menu.byId->insert((u64)(u32)item->id, item);
            destination.pushBack(item);
        }

        return item;
    }

    void pendingReply(DBusPendingCall*, void* data) {
        auto* pending = (Pending*)data;

        pending->menu->reply(*pending);
    }

    DBusHandlerResult registrarMessage(DBusConnection*, DBusMessage* msg, void* data) {
        ((MenusImpl*)data)->registrarMessage(msg);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    DBusHandlerResult busFilter(DBusConnection*, DBusMessage* msg, void* data) {
        auto* menus = (MenusImpl*)data;

        if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
            menus->nameOwnerChanged(msg);
        } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
            menus->signal(msg);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    bool validEndpoint(StringView service, StringView path) {
        Buffer s(service), p(path);
        DBusError error;

        dbus_error_init(&error);
        bool valid = dbus_validate_bus_name(s.cStr(), &error) && dbus_validate_path(p.cStr(), &error);
        dbus_error_free(&error);

        return valid;
    }

}

MenuImpl::MenuImpl(MenusImpl& p, ObjPool* own, StringView serviceName, StringView objectPath)
    : parent(&p)
    , pool(own)
{
    assign(service, serviceName);
    assign(path, objectPath);

    parent->menus.pushBack(this);
    resolveOwner();
    refresh();
}

MenuImpl::~MenuImpl() noexcept {
    for (Pending* p : pending) {
        if (p->call) {
            dbus_pending_call_cancel(p->call);
            dbus_pending_call_unref(p->call);
            p->call = nullptr;
        }

        parent->composer->alloc->release(p);
    }

    delete modelPool;
}

void MenuImpl::clearModel() {
    items.clear();
    delete modelPool;
    modelPool = nullptr;
    byId = nullptr;
    revision = 0;
    ready = false;
}

DBusMenuItem* MenuImpl::find(i32 id) {
    if (!byId) {
        return nullptr;
    }

    DBusMenuItem** item = byId->find((u64)(u32)id);

    return item ? *item : nullptr;
}

void MenuImpl::resolveOwner() {
    if (!text(service).empty() && text(service)[0] == ':') {
        assign(owner, text(service));

        return;
    }

    DBusMessage* msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");

    if (!msg) {
        return;
    }

    Buffer name(text(service));
    const char* requested = name.cStr();

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &requested, DBUS_TYPE_INVALID);
    parent->send(*this, msg, CallKind::owner, ++nextSequence);
}

void MenuImpl::refresh() {
    Buffer destination(text(service)), object(text(path));
    DBusMessage* msg = dbus_message_new_method_call(destination.cStr(), object.cStr(), kMenuInterface, "GetLayout");

    if (!msg) {
        return;
    }

    i32 root = 0, depth = -1;
    DBusMessageIter it, names;

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &root);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &depth);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &names);
    dbus_message_iter_close_container(&it, &names);

    newestLayout = ++nextSequence;
    parent->send(*this, msg, CallKind::layout, newestLayout);
}

void MenuImpl::prepare(i32 id) {
    Buffer destination(text(service)), object(text(path));
    DBusMessage* msg = dbus_message_new_method_call(destination.cStr(), object.cStr(), kMenuInterface, "AboutToShow");

    if (!msg) {
        return;
    }

    dbus_message_append_args(msg, DBUS_TYPE_INT32, &id, DBUS_TYPE_INVALID);
    parent->send(*this, msg, CallKind::aboutToShow, ++nextSequence);
}

void MenuImpl::activate(i32 id) {
    Buffer destination(text(service)), object(text(path));
    DBusMessage* msg = dbus_message_new_method_call(destination.cStr(), object.cStr(), kMenuInterface, "Event");

    if (!msg) {
        return;
    }

    DBusMessageIter it, value;
    const char* event = "clicked";
    i32 data = 0;
    u32 stamp = nowMsec();

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &id);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &event);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "i", &value);
    dbus_message_iter_append_basic(&value, DBUS_TYPE_INT32, &data);
    dbus_message_iter_close_container(&it, &value);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &stamp);
    dbus_connection_send(parent->conn, msg, nullptr);
    dbus_message_unref(msg);
}

void MenuImpl::reply(Pending& pendingCall) {
    DBusPendingCall* call = pendingCall.call;

    pendingCall.call = nullptr;

    for (size_t i = 0; i < pending.length(); i++) {
        if (pending[i] == &pendingCall) {
            pending.mut(i) = pending.back();
            pending.popBack();

            break;
        }
    }

    DBusMessage* reply = dbus_pending_call_steal_reply(call);

    dbus_pending_call_unref(call);

    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        if (reply) {
            dbus_message_unref(reply);
        }

        parent->composer->alloc->release(&pendingCall);

        return;
    }

    switch (pendingCall.kind) {
        case CallKind::owner: {
            const char* unique = "";

            if (dbus_message_get_args(reply, nullptr, DBUS_TYPE_STRING, &unique, DBUS_TYPE_INVALID)) {
                assign(owner, StringView(unique));
            }

            break;
        }
        case CallKind::layout:
            readLayout(reply, pendingCall.sequence);
            break;
        case CallKind::aboutToShow: {
            DBusMessageIter it;

            if (dbus_message_iter_init(reply, &it) && iterBool(&it)) {
                refresh();
            }

            break;
        }
    }

    dbus_message_unref(reply);
    parent->composer->alloc->release(&pendingCall);
}

void MenuImpl::readLayout(DBusMessage* reply, u64 sequence) {
    if (sequence != newestLayout) {
        return;
    }

    DBusMessageIter it;

    if (!dbus_message_iter_init(reply, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_UINT32) {
        return;
    }

    u32 nextRevision = iterU32(&it);

    if (!dbus_message_iter_next(&it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRUCT) {
        return;
    }

    ObjPool* previous = modelPool;
    IntMap<DBusMenuItem*>* previousMap = byId;
    Vector<DBusMenuItem*> nextItems;

    modelPool = ObjPool::fromMemoryRaw();
    byId = modelPool->make<IntMap<DBusMenuItem*>>(modelPool);

    int count = 0;

    readNode(*this, &it, nextItems, 0, count);

    if (count >= kMaxItems) {
        delete modelPool;
        modelPool = previous;
        byId = previousMap;

        return;
    }

    items.xchg(nextItems);
    revision = nextRevision;
    ready = true;
    delete previous;
    parent->composer->scene->needsFrame = true;
}

void MenuImpl::propertiesUpdated(DBusMessage* msg) {
    if (!byId) {
        return;
    }

    DBusMessageIter it;

    if (!dbus_message_iter_init(msg, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) {
        return;
    }

    DBusMessageIter updated;

    dbus_message_iter_recurse(&it, &updated);

    while (dbus_message_iter_get_arg_type(&updated) == DBUS_TYPE_STRUCT) {
        DBusMessageIter fields;

        dbus_message_iter_recurse(&updated, &fields);
        i32 id = iterI32(&fields);
        DBusMenuItem* item = find(id);

        if (item && dbus_message_iter_next(&fields)) {
            eachDict(&fields, [&](StringView key, DBusMessageIter* value) {
                readProperty(*this, *item, key, value);
            });
        }

        dbus_message_iter_next(&updated);
    }

    if (dbus_message_iter_next(&it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter removed;

        dbus_message_iter_recurse(&it, &removed);

        while (dbus_message_iter_get_arg_type(&removed) == DBUS_TYPE_STRUCT) {
            DBusMessageIter fields;

            dbus_message_iter_recurse(&removed, &fields);
            DBusMenuItem* item = find(iterI32(&fields));

            if (item && dbus_message_iter_next(&fields) && dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
                DBusMessageIter names;

                dbus_message_iter_recurse(&fields, &names);

                while (dbus_message_iter_get_arg_type(&names) == DBUS_TYPE_STRING) {
                    resetProperty(*item, iterString(&names));
                    dbus_message_iter_next(&names);
                }
            }

            dbus_message_iter_next(&removed);
        }
    }

    parent->composer->scene->needsFrame = true;
}

void MenuImpl::ownerChanged(StringView next) {
    assign(owner, next);

    if (next.empty()) {
        clearModel();
        parent->composer->scene->needsFrame = true;
    } else {
        refresh();
    }
}

MenusImpl::MenusImpl(Composer& c)
    : composer(&c)
    , conn(c.bus->raw())
{
    DBusObjectPathVTable vtable{};

    vtable.message_function = ::registrarMessage;
    dbus_connection_register_object_path(conn, kRegistrarPath, &vtable, this);
    dbus_connection_add_filter(conn, busFilter, this, nullptr);
    dbus_bus_add_match(conn, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'", nullptr);
    dbus_bus_add_match(conn, "type='signal',interface='com.canonical.dbusmenu'", nullptr);

    DBusError error;

    dbus_error_init(&error);
    int result = dbus_bus_request_name(conn, kRegistrar, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);

    ownsRegistrar = result == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;

    if (ownsRegistrar) {
        *(composer->log) << "imway: AppMenu registrar on the session bus"_sv << endL;
    } else {
        *(composer->log) << "imway: AppMenu registrar is taken ("_sv << result << ")"_sv << endL;
    }

    dbus_error_free(&error);
}

MenusImpl::~MenusImpl() noexcept {
    while (MenuImpl* menu = (MenuImpl*)menus.front()) {
        disconnect(menu);
    }

    for (Registration* registration : registrations) {
        composer->alloc->release(registration);
    }
}

DBusMenu* MenusImpl::connect(StringView service, StringView path) {
    if (!validEndpoint(service, path)) {
        *(composer->log) << "imway: rejected invalid DBusMenu endpoint "_sv << service << " "_sv << path << endL;

        return nullptr;
    }

    ObjPool* pool = ObjPool::fromMemoryRaw();

    return pool->make<MenuImpl>(*this, pool, service, path);
}

void MenusImpl::disconnect(DBusMenu* base) {
    if (!base) {
        return;
    }

    auto* menu = (MenuImpl*)base;
    ObjPool* pool = menu->pool;

    menu->unlink();
    delete pool;
}

bool MenusImpl::send(MenuImpl& menu, DBusMessage* msg, CallKind kind, u64 sequence) {
    DBusPendingCall* call = nullptr;

    if (!dbus_connection_send_with_reply(conn, msg, &call, kTimeout) || !call) {
        dbus_message_unref(msg);

        return false;
    }

    Pending* pending = composer->alloc->make<Pending>();

    pending->menu = &menu;
    pending->call = call;
    pending->kind = kind;
    pending->sequence = sequence;
    menu.pending.pushBack(pending);

    if (!dbus_pending_call_set_notify(call, pendingReply, pending, nullptr)) {
        menu.pending.popBack();
        dbus_pending_call_cancel(call);
        dbus_pending_call_unref(call);
        composer->alloc->release(pending);
        dbus_message_unref(msg);

        return false;
    }

    dbus_message_unref(msg);

    return true;
}

Registration* MenusImpl::registration(u32 window) {
    for (Registration* candidate : registrations) {
        if (candidate->window == window) {
            return candidate;
        }
    }

    return nullptr;
}

void MenusImpl::emitRegistered(const char* member, const Registration& reg) {
    DBusMessage* signal = dbus_message_new_signal(kRegistrarPath, kRegistrar, member);

    if (!signal) {
        return;
    }

    Buffer service(text(reg.service)), path(text(reg.path));
    const char* s = service.cStr();
    const char* p = path.cStr();
    u32 window = reg.window;

    if (StringView(member) == "WindowRegistered"_sv) {
        dbus_message_append_args(signal, DBUS_TYPE_UINT32, &window, DBUS_TYPE_STRING, &s, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
    } else {
        dbus_message_append_args(signal, DBUS_TYPE_UINT32, &window, DBUS_TYPE_INVALID);
    }

    dbus_connection_send(conn, signal, nullptr);
    dbus_message_unref(signal);
}

void MenusImpl::eraseRegistration(size_t index, bool emit) {
    Registration* reg = registrations[index];

    if (emit) {
        emitRegistered("WindowUnregistered", *reg);
    }

    registrations.mut(index) = registrations.back();
    registrations.popBack();
    composer->alloc->release(reg);
}

void MenusImpl::registrarMessage(DBusMessage* msg) {
    const char* sender = dbus_message_get_sender(msg);

    if (dbus_message_is_method_call(msg, kRegistrar, "RegisterWindow")) {
        u32 window = 0;
        const char* path = "";

        if (!sender || !dbus_message_get_args(msg, nullptr, DBUS_TYPE_UINT32, &window, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID)) {
            return;
        }

        Registration* reg = registration(window);

        if (!reg) {
            reg = composer->alloc->make<Registration>();
            reg->window = window;
            registrations.pushBack(reg);
        }

        assign(reg->sender, StringView(sender));
        assign(reg->service, StringView(sender));
        assign(reg->path, StringView(path));
        emitRegistered("WindowRegistered", *reg);
    } else if (dbus_message_is_method_call(msg, kRegistrar, "UnregisterWindow")) {
        u32 window = 0;

        if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_UINT32, &window, DBUS_TYPE_INVALID)) {
            return;
        }

        for (size_t i = 0; i < registrations.length(); i++) {
            if (registrations[i]->window == window && (!sender || text(registrations[i]->sender) == StringView(sender))) {
                eraseRegistration(i, true);

                break;
            }
        }
    } else if (dbus_message_is_method_call(msg, kRegistrar, "GetMenuForWindow")) {
        u32 window = 0;

        if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_UINT32, &window, DBUS_TYPE_INVALID)) {
            return;
        }

        Registration* reg = registration(window);

        if (!reg) {
            DBusMessage* error = dbus_message_new_error(msg, "com.canonical.AppMenu.Registrar.Error.WindowNotFound", "window is not registered");

            dbus_connection_send(conn, error, nullptr);
            dbus_message_unref(error);

            return;
        }

        DBusMessage* reply = dbus_message_new_method_return(msg);
        Buffer service(text(reg->service)), path(text(reg->path));
        const char* s = service.cStr();
        const char* p = path.cStr();

        dbus_message_append_args(reply, DBUS_TYPE_STRING, &s, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);

        return;
    } else if (dbus_message_is_method_call(msg, kRegistrar, "GetMenus")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, array;

        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(uso)", &array);

        for (Registration* reg : registrations) {
            DBusMessageIter row;
            Buffer service(text(reg->service)), path(text(reg->path));
            const char* s = service.cStr();
            const char* p = path.cStr();
            u32 window = reg->window;

            dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, nullptr, &row);
            dbus_message_iter_append_basic(&row, DBUS_TYPE_UINT32, &window);
            dbus_message_iter_append_basic(&row, DBUS_TYPE_STRING, &s);
            dbus_message_iter_append_basic(&row, DBUS_TYPE_OBJECT_PATH, &p);
            dbus_message_iter_close_container(&array, &row);
        }

        dbus_message_iter_close_container(&it, &array);
        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);

        return;
    } else {
        return;
    }

    DBusMessage* reply = dbus_message_new_method_return(msg);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void MenusImpl::nameOwnerChanged(DBusMessage* msg) {
    const char *name = "", *oldOwner = "", *newOwner = "";

    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &oldOwner, DBUS_TYPE_STRING, &newOwner, DBUS_TYPE_INVALID)) {
        return;
    }

    for (MenuImpl* menu : each<MenuImpl>(menus)) {
        if (text(menu->service) == StringView(name)) {
            menu->ownerChanged(StringView(newOwner));
        }
    }

    if (!oldOwner[0] || newOwner[0]) {
        return;
    }

    for (size_t i = registrations.length(); i > 0; i--) {
        if (text(registrations[i - 1]->sender) == StringView(oldOwner)) {
            eraseRegistration(i - 1, true);
        }
    }
}

void MenusImpl::signal(DBusMessage* msg) {
    StringView sender(dbus_message_get_sender(msg) ? dbus_message_get_sender(msg) : "");
    StringView path(dbus_message_get_path(msg) ? dbus_message_get_path(msg) : "");

    for (MenuImpl* menu : each<MenuImpl>(menus)) {
        if (text(menu->path) != path || (text(menu->service) != sender && text(menu->owner) != sender)) {
            continue;
        }

        if (dbus_message_is_signal(msg, kMenuInterface, "LayoutUpdated")) {
            DBusMessageIter it;

            if (dbus_message_iter_init(msg, &it)) {
                u32 nextRevision = iterU32(&it);

                if (!menu->ready || nextRevision == 0 || nextRevision > menu->revision) {
                    menu->refresh();
                }
            }
        } else if (dbus_message_is_signal(msg, kMenuInterface, "ItemsPropertiesUpdated")) {
            menu->propertiesUpdated(msg);
        } else if (dbus_message_is_signal(msg, kMenuInterface, "ItemActivationRequested")) {
            DBusMessageIter it;

            if (dbus_message_iter_init(msg, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_INT32) {
                menu->activationRequested = iterI32(&it);
                menu->hasActivationRequest = true;
                composer->scene->needsFrame = true;
            }
        }
    }
}

DBusMenus* DBusMenus::create(Composer& c) {
    return c.pool->make<MenusImpl>(c);
}
