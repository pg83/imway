#include "screenshot_capture.h"

#include "composer.h"
#include "device_vk.h"
#include "main_supervisor.h"
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
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/atomic.h>
#include <std/sys/event_fd.h>
#include <std/sys/types.h>
#include <std/thr/pool.h>

using namespace stl;

namespace {
    struct ScreenshotCaptureImpl: ScreenshotCapture {
        Composer* comp = nullptr;
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
        EventFD done;
        ev_io* doneIo = nullptr;
        bool busy_ = false;
        bool fencePending = false;
        bool workerPending = false;
        int resultFd = -1;

        ScreenshotCaptureImpl(Composer& c, const DeviceVk& vk, int w, int h,
                              VkFormat fmt, float scale);
        ~ScreenshotCaptureImpl() noexcept;

        bool busy() const override;
        bool submit(VkImage image, VkImageLayout layout) override;
        void pollFence();
        int buildFile();
        void ready();
        void spawn(int mfd);
    };

    void fenceTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((ScreenshotCaptureImpl*)w->data)->pollFence();
    }

    void doneIoCb(struct ev_loop*, ev_io* w, int) {
        ((ScreenshotCaptureImpl*)w->data)->ready();
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
                                             float scale)
    : comp(&c)
    , phys(vk.phys)
    , device(vk.device)
    , queue(vk.queue)
    , queueFamily(vk.queueFamily)
    , width(w)
    , height(h)
    , format(fmt)
    , uiScale(scale)
{
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
    VK_CHECK(vkMapMemory(device, readbackMemory, 0, VK_WHOLE_SIZE, 0, &readbackMap));

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

bool ScreenshotCaptureImpl::submit(VkImage image, VkImageLayout layout) {
    if (busy_) {
        return false;
    }

    vkResetCommandBuffer(command, 0);
    vkResetFences(device, 1, &fence);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &bi);

    // The composed frame submission precedes this one on the same queue.
    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = layout;
    bar.newLayout = layout;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = image;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &bar);

    VkBufferImageCopy region{};

    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(u32)width, (u32)height, 1};
    vkCmdCopyImageToBuffer(command, image, layout, readback, 1, &region);
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

    busy_ = true;
    fencePending = true;
    ev_timer_set(fenceTimer, 0.001, 0.001);
    ev_timer_start(comp->loop, fenceTimer);

    return true;
}

void ScreenshotCaptureImpl::pollFence() {
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
    auto* chunk = (u32*)malloc(chunkCapacity);

    if (!chunk || !writeAll(&header, sizeof(header))) {
        free(chunk);
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
            free(chunk);
            close(mfd);

            return -1;
        }

        offset += chunkBytes;
    }

    free(chunk);

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

    spawn(mfd);
}

void ScreenshotCaptureImpl::spawn(int mfd) {
    StringView args[] = {"/proc/self/exe"_sv, "screenshot"_sv,
                         "/proc/self/fd/3"_sv};
    StringBuilder display;

    display << "WAYLAND_DISPLAY="_sv << comp->scene->socketName;

    StringBuilder scale;

    scale << "IMGUI_SCALE="_sv << (long double)uiScale;

    StringView env[] = {sv(display), sv(scale)};
    SupervisorSpawn spec;

    spec.args = args;
    spec.argCount = 3;
    spec.env = env;
    spec.envCount = 2;
    spec.passFd = mfd;
    spec.targetFd = 3;

    i32 pid = comp->supervisor->spawn(spec);

    if (pid < 0) {
        sysE << "imway: screenshot spawn failed: "_sv
             << (const char*)strerror(-pid) << endL;
    }

    close(mfd);
}

ScreenshotCapture* ScreenshotCapture::create(Composer& c, const DeviceVk& vk,
                                             int width, int height,
                                             VkFormat format, float uiScale) {
    return c.pool->make<ScreenshotCaptureImpl>(c, vk, width, height, format,
                                                uiScale);
}
