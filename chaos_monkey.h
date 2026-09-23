#pragma once

#include <vulkan/vulkan.h>

#include <std/sys/types.h>

#include <stddef.h>
#include <sys/types.h>

namespace stl {
    class ObjPool;
}

struct passwd;
struct wl_resource;
struct pam_message;
struct pam_response;
struct DBusMessage;
struct DBusConnection;
struct libinput;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

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
    // wayland shm and linux-dmabuf
    // a wl_shm pool's mapping fresh from mmap (MAP_FAILED when it failed); a
    // replacement failure unmaps the size it was handed and returns MAP_FAILED
    virtual void* shmMap(void* mapped, size_t size) = 0;
    // a thread's SIGBUS guard record fresh from calloc, made on the
    // thread's first access to a client's shm memory; a replacement failure
    // frees it and returns null
    virtual void* sigbusRecord(void* allocated) = 0;
    // drmPrimeFDToHandle's result on a client's dma-buf plane, the driver's
    // verdict on the buffer; a failure also sets errno
    virtual int primeImport(int result) = 0;
    // getrandom's answer for an xdg-activation token's random part (the
    // byte count, or -1 with errno set)
    virtual long entropy(long got) = 0;
    // security-context: a sandboxed client's connection fresh from accept4
    // (or its -1); a replacement failure closes the fd and returns -1
    virtual int securityAccept(int fd) = 0;
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
    // fence polls: one poll's reading of a GPU readback's fence (the
    // screenshot's or the frame capture's); VK_NOT_READY keeps the poll
    // waiting, as for a GPU still busy with the copy
    virtual VkResult readbackPoll(VkResult status) = 0;
    // the screenshot's file, built on the offload lane: the memfd fresh
    // from memfd_create (a replacement failure closes it and returns -1),
    // then the result of each write into it
    virtual int shotFile(int fd) = 0;
    virtual ssize_t shotWrite(ssize_t written) = 0;
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
    // targets: at boot, again whenever the output changes size, and for
    // scanout buffers the output replaced at the same size
    virtual VkResult outputTarget(VkResult result) = 0;
    // whether the Vulkan device offers the named extension, as the device
    // answered: a device without it takes the fallback of its own
    virtual bool deviceExtension(const char* name, bool offered) = 0;
    // renderer: queue submits (frames, readbacks, captures, cursor shapes)
    // the outcome a submit is about to have, handed over before the call:
    // a failure stands for the driver refusing it, and nothing is submitted
    virtual VkResult frameSubmit(VkResult pending) = 0;
    virtual VkResult readbackSubmit(VkResult pending) = 0;
    virtual VkResult captureSubmit(VkResult pending) = 0;
    virtual VkResult cursorSubmit(VkResult pending) = 0;
    // the result of a Vulkan call building the session's once-only GPU
    // objects at boot: the instance and device, the renderer's passes,
    // pipelines and pools, the texture chain's layout, the capture's pool
    virtual VkResult setup(VkResult result) = 0;

    // renderer: wl_shm imports and the screenshot capture
    // the device's memory types as a wl_shm pool's import (a host pointer
    // or a udmabuf buffer) picks its heap from them
    virtual void poolMemoryTypes(VkPhysicalDeviceMemoryProperties& props) = 0;
    // the outcome the screenshot capture's copy submit is about to have,
    // under the same rule as the other submits
    virtual VkResult shotSubmit(VkResult pending) = 0;
    // whether a wl_shm pool's udmabuf took the CPU-access bracket that lets
    // the GPU read it (DMA_BUF_IOCTL_SYNC); a refusal sets errno
    virtual bool udmabufRead(bool started) = 0;
    // the result of a bounded wait on a fence the caller cannot go on
    // without (vkWaitOrDie): a readback, a cursor shape, the capture's
    // teardown
    virtual VkResult gpuWait(VkResult result) = 0;

    // screenshot viewer (imway screenshot, its own process and monkey)
    // the result of acquiring a swapchain image or presenting one
    virtual VkResult swapchain(VkResult result) = 0;

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

    // spawn: the /dev/null the spawner opens once for every child's stdio
    // (or its -1); a replacement failure closes the fd it was handed
    virtual int devNull(int fd) = 0;

    // renderer: texture descriptor pools
    // a set allocation's result from the chain's pool number `pool`, after
    // descriptorSet: a pool reported full or fragmented is passed over for
    // the next one in the chain
    virtual VkResult descriptorRoom(VkResult result, size_t pool) = 0;

    // device: /dev/udmabuf fresh from open (or its -1); a replacement
    // failure closes the fd and returns -1 with errno set
    virtual int udmabufOpen(int fd) = 0;

    // screenshot viewer: its encoders
    // the outcome an encoder allocation is about to have (libpng's write
    // and info structs, libjxl's encoder and frame settings), handed over
    // before the call: false stands for it failing, and nothing is made
    virtual bool encoderAlloc(bool pending) = 0;

    // the 32-bit millisecond clock (nowMsec) as the sites that stamp and age
    // things by it read it: the bell, the OSD, the stats sample
    virtual u32 clockMs(u32 ms) = 0;

    // vulkan: the device the session picks, and what the renderer builds
    // on it outside the boot's setup
    // the number of devices the instance enumerated; none stands for a
    // system without a usable driver
    virtual u32 vulkanDevices(u32 found) = 0;
    // one queue family's capabilities as the device reports them: a device
    // with no graphics family cannot draw the session
    virtual VkQueueFlags queueFlags(VkQueueFlags flags) = 0;
    // the result of one Vulkan call building an icon's texture (the dock's,
    // the launcher's, a notification's, a tab's)
    virtual VkResult iconTexture(VkResult result) = 0;
    // the result of one Vulkan call building the screenshot capture's
    // readback buffer, sized by the output at the capture
    virtual VkResult shotReadback(VkResult result) = 0;

    // the resources a subsystem checks for before it goes on
    // control FIFO (test build): its read end fresh from open (or its -1);
    // a replacement failure closes the fd and returns -1 with errno set
    virtual int controlOpen(int fd) = 0;
    // input: the libinput context fresh from libinput_path_create_context;
    // a replacement failure unrefs it and returns null
    virtual libinput* libinputContext(libinput* made) = 0;
    // keyboard: the xkb context, a keymap fresh from compilation (null when
    // it did not compile) and its state, each as xkbcommon made it; a
    // replacement failure unrefs what it was handed and returns null
    virtual xkb_context* xkbContext(xkb_context* made) = 0;
    virtual xkb_keymap* xkbKeymap(xkb_keymap* compiled) = 0;
    virtual xkb_state* xkbState(xkb_state* made) = 0;
    // the keymap's sealed file: its memfd fresh from memfd_create (a
    // replacement failure closes it and returns -1), then the write of the
    // keymap text into it
    virtual int keymapFile(int fd) = 0;
    virtual ssize_t keymapWrite(ssize_t written) = 0;

    static ChaosMonkey* create(stl::ObjPool& pool);
};
