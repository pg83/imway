#pragma once

#include <vulkan/vulkan.h>

namespace stl {
    class ObjPool;
}

struct passwd;
struct wl_resource;
struct pam_message;
struct pam_response;
struct DBusMessage;
struct DBusConnection;

// The fault seam, one per board (Composer::chaos). A call site hands over
// the object it is about to act on, or the result it has just got, and
// carries on with whatever comes back. The production build gives
// everything back untouched; the test build (IMWAY_FOR_TESTS) breaks what
// IMWAY_CHAOS asks for. Sites call from whatever thread they run
// on: the lockscreen's PAM ones come from the offload lane.
struct ChaosMonkey {
    // the account the lockscreen authenticates, as getpwuid found it
    virtual passwd* account(passwd* found) = 0;
    // one prompt of a PAM conversation, before the lockscreen answers it
    virtual const pam_message* pamMessage(const pam_message* message) = 0;
    // the conversation's response array fresh from calloc; a replacement
    // takes over the one it was handed
    virtual pam_response* pamResponses(pam_response* responses) = 0;
    // one answer's copy fresh from strdup, under the same ownership rule
    virtual char* pamAnswer(char* answer) = 0;
    // the device's memory types, before a heap is picked out of them
    virtual void memoryTypes(VkPhysicalDeviceMemoryProperties& props) = 0;
    // the result of a Vulkan call its caller checks
    virtual VkResult vulkan(VkResult result) = 0;

    // wayland: a resource fresh from wl_resource_create, before the request
    // or bind that asked for it fills it in; a null return stands for the
    // allocation failing (the replacement destroys what it was handed)
    virtual wl_resource* resource(wl_resource* created) = 0;
    // KMS backend
    // the result of a Vulkan call building or exporting a scanout buffer
    virtual VkResult scanout(VkResult result) = 0;
    // the driver's answer on whether it can build a scanout image with a
    // candidate modifier
    virtual VkResult scanoutModifier(VkResult result) = 0;
    // the answer of the VT_GETSTATE query that finds the session's VT
    virtual int vtState(int result) = 0;
    // the session's VT just opened; a failure takes over the fd
    virtual int vtOpen(int fd) = 0;

    // wayland drm-lease: the fd the device's lease creation returned (or its
    // negative errno); a replacement failure closes the fd it was handed
    virtual int leaseFd(int fd) = 0;
    // renderer: client buffers onto the GPU
    // the result of one Vulkan call that imports a client's buffer: a
    // dma-buf image, or a wl_shm pool as a udmabuf or a host pointer
    virtual VkResult clientImport(VkResult result) = 0;
    // the result of one Vulkan call allocating a GPU resource sized by a
    // client's buffer: its texture, staging or upload buffer
    virtual VkResult clientTexture(VkResult result) = 0;
    // a finished frame's fence: its status, then its reset
    virtual VkResult frameFence(VkResult result) = 0;
    // a GPU readback's fence as its poll delivers it: the screenshot's, or
    // the frame capture's that screencopy clients wait on
    virtual VkResult readbackFence(VkResult result) = 0;
    // the texture descriptor chain: a pool being created for it, and a set
    // being allocated from one of its pools
    virtual VkResult descriptorPool(VkResult result) = 0;
    virtual VkResult descriptorSet(VkResult result) = 0;
    // a sync file the renderer exported: to wait on before sampling a
    // client's dma-buf (implicit or explicit sync), or its own frame's
    // fence to hand back to those dma-bufs; a replacement failure closes
    // the fd it was handed and returns -1
    virtual int syncFile(int fd) = 0;
    // the result of making or filling a sync-file semaphore: one the frame
    // waits on (creating it, importing the fd into it), or the frame's own
    // signal semaphore being recreated
    virtual VkResult syncWait(VkResult result) = 0;
    // the result of one Vulkan call building the renderer's output-sized
    // targets: at boot, and again whenever the output changes size
    virtual VkResult outputTarget(VkResult result) = 0;

    // the buses (dbus_menu, status_notifier, wifi): a message a site has
    // just built, before it goes anywhere (a replacement takes over the one
    // it was handed); a call about to be sent, null to have the send fail
    // with the site still holding its message; whether a sent call's reply
    // notify went in
    virtual DBusMessage* dbusMessage(DBusMessage* built) = 0;
    virtual DBusMessage* dbusSend(DBusMessage* call) = 0;
    virtual bool dbusNotify(DBusMessage* sent, bool installed) = 0;
    // a bus connection fresh from the bus, before the loop takes over its
    // watches: the test build may shrink its socket's send buffer and its
    // incoming queue, so libdbus has to toggle them under backpressure
    virtual void dbusConnection(DBusConnection* conn) = 0;

    static ChaosMonkey* create(stl::ObjPool& pool);
};
