#include "screenshot_capture.h"

#include "composer.h"
#include "device_vk.h"
#include "main_supervisor.h"
#include "listener.h"
#include "output.h"
#include "pooled_ev.h"
#include "scene.h"
#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ev.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/atomic.h>
#include <std/sys/event_fd.h>
#include <std/sys/types.h>
#include <std/thr/pool.h>

using namespace stl;

namespace {
    struct ScreenshotCaptureImpl: ScreenshotCapture, Listener {
        struct Retained {
            int fd;
            ev_tstamp until;
        };

        Composer* comp = nullptr;
        ::Output* output = nullptr;
        Listener* renderReady = nullptr;
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        u32 queueFamily = 0;
        int width = 0;
        int height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        float uiScale = 1.f;

        VkBuffer readback = VK_NULL_HANDLE;
        VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
        void* readbackMap = nullptr;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;

        ev_timer* fenceTimer = nullptr;
        ev_timer* retentionTimer = nullptr;
        EventFD done;
        ev_io* doneIo = nullptr;
        bool busy_ = false;
        bool fencePending = false;
        bool workerPending = false;
        bool handoff = false;
        bool waitingRetire = false;
        int resultFd = -1;
        SharedScanout shared;
        Vector<Retained> retained;

        ScreenshotCaptureImpl(Composer& c, const DeviceVk& vk, int w, int h,
                              VkFormat fmt, float scale, Listener& ready);
        ~ScreenshotCaptureImpl() noexcept;

        bool busy() const override;
        void request() override;
        bool submit(int scanoutIndex, VkImage image,
                    VkImageLayout layout) override;
        void onListen(void* data) override;
        void ensureReadback();
        void pollFence();
        int buildFile();
        void ready();
        void spawn(int fd, const SharedScanout* image);
        void retain(int mfd);
        void releaseExpired();
    };

    void fenceTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((ScreenshotCaptureImpl*)w->data)->pollFence();
    }

    void doneIoCb(struct ev_loop*, ev_io* w, int) {
        ((ScreenshotCaptureImpl*)w->data)->ready();
    }

    void retentionTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((ScreenshotCaptureImpl*)w->data)->releaseExpired();
    }

    u32 findMemoryType(VkPhysicalDevice phys, u32 typeBits, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp{};

        vkGetPhysicalDeviceMemoryProperties(phys, &mp);

        for (u32 i = 0; i < mp.memoryTypeCount; i++) {
            if ((typeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }

        return UINT32_MAX;
    }
}

ScreenshotCaptureImpl::ScreenshotCaptureImpl(Composer& c, const DeviceVk& vk,
                                             int w, int h, VkFormat fmt,
                                             float scale, Listener& ready)
    : comp(&c)
    , output(c.output)
    , renderReady(&ready)
    , phys(vk.phys)
    , device(vk.device)
    , queue(vk.queue)
    , queueFamily(vk.queueFamily)
    , width(w)
    , height(h)
    , format(fmt)
    , uiScale(scale)
{
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queueFamily;
    VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

    cbai.commandPool = commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &command));

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

    VK_CHECK(vkCreateFence(device, &fci, nullptr, &fence));

    fenceTimer = createEvTimer(*c.pool, c.loop);
    ev_timer_init(fenceTimer, fenceTimerCb, 0., 0.);
    fenceTimer->data = this;

    retentionTimer = createEvTimer(*c.pool, c.loop);
    ev_timer_init(retentionTimer, retentionTimerCb, 0., 0.);
    retentionTimer->data = this;

    doneIo = createEvIo(*c.pool, c.loop);
    ev_io_init(doneIo, doneIoCb, done.fd(), EV_READ);
    doneIo->data = this;
    ev_io_start(c.loop, doneIo);
}

ScreenshotCaptureImpl::~ScreenshotCaptureImpl() noexcept {
    if (ev_is_active(fenceTimer)) {
        ev_timer_stop(comp->loop, fenceTimer);
    }
    if (ev_is_active(doneIo)) {
        ev_io_stop(comp->loop, doneIo);
    }
    if (ev_is_active(retentionTimer)) {
        ev_timer_stop(comp->loop, retentionTimer);
    }

    if (fencePending) {
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    if (workerPending) {
        comp->offload->join();
    }

    int mfd = stdAtomicFetch(&resultFd, MemoryOrder::Acquire);

    if (mfd >= 0) {
        close(mfd);
    }

    if (shared.fd >= 0) {
        close(shared.fd);
    }

    for (const Retained& item : retained) {
        close(item.fd);
    }

    if (readbackMap) {
        vkUnmapMemory(device, readbackMemory);
    }
    if (fence) {
        vkDestroyFence(device, fence, nullptr);
    }
    if (commandPool) {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }
    if (readback) {
        vkDestroyBuffer(device, readback, nullptr);
    }
    if (readbackMemory) {
        vkFreeMemory(device, readbackMemory, nullptr);
    }
}

bool ScreenshotCaptureImpl::busy() const {
    return busy_;
}

void ScreenshotCaptureImpl::request() {
    if (busy_) {
        return;
    }

    busy_ = true;
    handoff = output->prepareScreenshot(*this);

    if (!handoff) {
        renderReady->onListen(this);
    }
}

void ScreenshotCaptureImpl::onListen(void* data) {
    handoff = data != nullptr;
    renderReady->onListen(this);
}

void ScreenshotCaptureImpl::ensureReadback() {
    if (readback) {
        return;
    }

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};

    bci.size = (VkDeviceSize)width * height * sizeof(u32);
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK(vkCreateBuffer(device, &bci, nullptr, &readback));

    VkMemoryRequirements req{};

    vkGetBufferMemoryRequirements(device, readback, &req);

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &readbackMemory));
    VK_CHECK(vkBindBufferMemory(device, readback, readbackMemory, 0));
    VK_CHECK(vkMapMemory(device, readbackMemory, 0, VK_WHOLE_SIZE, 0,
                        &readbackMap));
}

bool ScreenshotCaptureImpl::submit(int scanoutIndex, VkImage image,
                                   VkImageLayout layout) {
    if (!busy_ || fencePending || workerPending || waitingRetire) {
        return false;
    }

    if (handoff && !output->takeScreenshot(scanoutIndex, shared)) {
        handoff = false;
    }

    if (!handoff) {
        ensureReadback();
    }

    vkResetCommandBuffer(command, 0);
    vkResetFences(device, 1, &fence);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &bi);

    // The composed frame submission precedes this one on the same queue.
    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = handoff ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = layout;
    bar.newLayout = layout;
    bar.srcQueueFamilyIndex = handoff ? queueFamily : VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = handoff ? VK_QUEUE_FAMILY_EXTERNAL :
                                        VK_QUEUE_FAMILY_IGNORED;
    bar.image = image;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         handoff ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT :
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &bar);

    if (!handoff) {
        VkBufferImageCopy region{};

        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(u32)width, (u32)height, 1};
        vkCmdCopyImageToBuffer(command, image, layout, readback, 1, &region);
    }
    vkEndCommandBuffer(command);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &command;

    VkResult result = vkQueueSubmit(queue, 1, &si, fence);

    if (result != VK_SUCCESS) {
        sysE << "imway: screenshot submit failed ("_sv << (long)result
             << ")"_sv << endL;

        return false;
    }

    fencePending = true;
    comp->scene->needsFrame = handoff;
    ev_timer_set(fenceTimer, 0.001, 0.001);
    ev_timer_start(comp->loop, fenceTimer);

    return true;
}

void ScreenshotCaptureImpl::pollFence() {
    if (waitingRetire) {
        if (output->screenshotPending()) {
            return;
        }

        ev_timer_stop(comp->loop, fenceTimer);
        waitingRetire = false;
        handoff = false;
        busy_ = false;

        return;
    }

    VkResult status = vkGetFenceStatus(device, fence);

    if (status == VK_NOT_READY) {
        return;
    }

    ev_timer_stop(comp->loop, fenceTimer);
    fencePending = false;

    if (status != VK_SUCCESS) {
        busy_ = false;
        sysE << "imway: screenshot fence failed ("_sv << (long)status
             << ")"_sv << endL;

        return;
    }

    if (handoff) {
        int fd = shared.fd;

        shared.fd = -1;
        spawn(fd, &shared);
        waitingRetire = output->screenshotPending();

        if (waitingRetire) {
            ev_timer_set(fenceTimer, 0.001, 0.001);
            ev_timer_start(comp->loop, fenceTimer);
        } else {
            busy_ = false;
            handoff = false;
        }

        return;
    }

    workerPending = true;
    comp->offload->submit([this] {
        int mfd = buildFile();

        stdAtomicStore(&resultFd, mfd, MemoryOrder::Release);
        done.signal();
    });
}

int ScreenshotCaptureImpl::buildFile() {
    int mfd = memfd_create("imway-shot", MFD_CLOEXEC);

    if (mfd < 0) {
        return -1;
    }

    struct {
        u32 magic;
        u32 w;
        u32 h;
    } header = {0x31574d49u, (u32)width, (u32)height};

    auto writeAll = [mfd](const void* data, size_t size) {
        auto* p = (const u8*)data;

        while (size) {
            ssize_t n = write(mfd, p, size);

            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return false;
            }
            p += n;
            size -= (size_t)n;
        }

        return true;
    };

    constexpr size_t chunkCapacity = 1024 * 1024;
    Vector<u32> chunkBuf;

    chunkBuf.zero(chunkCapacity / sizeof(u32));

    u32* chunk = chunkBuf.mutData();

    if (!writeAll(&header, sizeof(header))) {
        close(mfd);

        return -1;
    }

    const u8* input = (const u8*)readbackMap;
    size_t bytes = (size_t)width * height * sizeof(u32);

    for (size_t offset = 0; offset < bytes;) {
        size_t chunkBytes = bytes - offset;

        if (chunkBytes > chunkCapacity) {
            chunkBytes = chunkCapacity;
        }

        // The Vulkan allocation may be WC/uncached. Bulk-copy into normal
        // cache before doing scalar conversion.
        memcpy(chunk, input + offset, chunkBytes);

        size_t count = chunkBytes / sizeof(u32);

        if (format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
            for (size_t i = 0; i < count; i++) {
                u32 src = chunk[i];

                chunk[i] = ((src >> 22) & 0xff) |
                           (((src >> 12) & 0xff) << 8) |
                           (((src >> 2) & 0xff) << 16) |
                           0xff000000u;
            }
        } else {
            for (size_t i = 0; i < count; i++) {
                u32 src = chunk[i];

                chunk[i] = ((src >> 16) & 0xff) |
                           (src & 0x0000ff00u) |
                           ((src & 0xff) << 16) |
                           0xff000000u;
            }
        }

        if (!writeAll(chunk, chunkBytes)) {
            close(mfd);

            return -1;
        }

        offset += chunkBytes;
    }

    return mfd;
}

void ScreenshotCaptureImpl::ready() {
    done.drain();

    int mfd = stdAtomicFetch(&resultFd, MemoryOrder::Acquire);

    stdAtomicStore(&resultFd, -1, MemoryOrder::Relaxed);
    workerPending = false;
    busy_ = false;

    if (mfd < 0) {
        sysE << "imway: screenshot readback failed"_sv << endL;

        return;
    }

    spawn(mfd, nullptr);
}

void ScreenshotCaptureImpl::spawn(int fd, const SharedScanout* image) {
    StringBuilder source;

    source << "/proc/"_sv << (long)getpid() << "/fd/"_sv << fd;

    StringView args[] = {"/proc/self/exe"_sv, "screenshot"_sv, sv(source)};
    StringBuilder display;

    display << "WAYLAND_DISPLAY="_sv << comp->scene->socketName;

    StringBuilder scale;

    scale << "IMGUI_SCALE="_sv << (long double)uiScale;

    StringBuilder metadata;
    StringBuilder color;
    const OutputColorState& shotColor = image ? image->color : output->colorState();
    bool hdr = shotColor.hdr();
    double sdrWhite = hdr ? shotColor.sdrWhiteNits : 0;

    color << "IMWAY_SHOT_COLOR="_sv << (hdr ? 1 : 0) << ":"_sv
          << (long double)sdrWhite << ":"_sv
          << (long double)shotColor.displayMinNits << ":"_sv
          << (long double)shotColor.displayPeakNits << ":"_sv
          << (long double)shotColor.displayMaxFallNits;

    if (image) {
        metadata << "IMWAY_SHOT_DMABUF="_sv
                 << (unsigned long long)image->width << ":"_sv
                 << (unsigned long long)image->height << ":"_sv
                 << (unsigned long long)image->format << ":"_sv
                 << (unsigned long long)image->offset << ":"_sv
                 << (unsigned long long)image->stride << ":"_sv
                 << (unsigned long long)image->modifier << ":"_sv
                 << (unsigned long long)image->allocationSize << ":"_sv
                 << (unsigned long long)image->renderDevice;
    }

    StringView env[] = {sv(display), sv(scale), sv(color), sv(metadata)};
    SupervisorSpawn spec;

    spec.args = args;
    spec.argCount = 3;
    spec.env = env;
    spec.envCount = image ? 4 : 3;

    comp->supervisor->spawn(spec);
    retain(fd);
}

void ScreenshotCaptureImpl::retain(int mfd) {
    retained.pushBack({mfd, ev_now(comp->loop) + 10.});

    if (!ev_is_active(retentionTimer)) {
        ev_timer_set(retentionTimer, 10., 0.);
        ev_timer_start(comp->loop, retentionTimer);
    }
}

void ScreenshotCaptureImpl::releaseExpired() {
    ev_tstamp now = ev_now(comp->loop);
    ev_tstamp next = 0.;

    for (size_t i = 0; i < retained.length();) {
        if (retained[i].until <= now) {
            close(retained[i].fd);
            retained.mut(i) = retained.back();
            retained.popBack();
        } else {
            if (!next || retained[i].until < next) {
                next = retained[i].until;
            }
            i++;
        }
    }

    if (next) {
        ev_timer_set(retentionTimer, next - now, 0.);
        ev_timer_start(comp->loop, retentionTimer);
    }
}

ScreenshotCapture* ScreenshotCapture::create(Composer& c, const DeviceVk& vk,
                                             int width, int height,
                                             VkFormat format, float uiScale,
                                             Listener& ready) {
    return c.pool->make<ScreenshotCaptureImpl>(c, vk, width, height, format,
                                                uiScale, ready);
}
