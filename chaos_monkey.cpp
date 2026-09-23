#include "chaos_monkey.h"

#include "util.h"

#include <std/str/view.h>
#include <std/mem/obj_pool.h>

#ifdef IMWAY_FOR_TESTS
    #if __has_include(<security/pam_appl.h>)
        #include <security/pam_appl.h>
    #endif

    #include <errno.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/mman.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <std/lib/vector.h>
    #include <wayland-server-core.h>

    #include <dbus/dbus.h>
#endif

using namespace stl;

#ifdef IMWAY_FOR_TESTS
// The test binary's monkey. IMWAY_CHAOS lists the faults as FAULT=ARG words,
// read once at boot, and each fault is spent by the call it fires on, so a
// scenario states exactly which call goes wrong:
//   account=N         the next N account lookups come back empty
//   pam-message=S     the next PAM prompt is rewritten to style S, or
//                     dropped (a null message) for S=drop
//   pam-responses=N   the next N response arrays fail to allocate
//   pam-answer=N      the next N answer copies fail to allocate
//   memory-types=N    the next N memory-type queries find none
//   vulkan=K          K checked Vulkan calls pass, the one after fails
//   resource=IFACE    wayland: the next resource of wl_interface IFACE
//                     (wl_shm_pool, xdg_popup, ...) fails to allocate; one
//                     word per interface, each spent on its own
//   shm-map=K         wayland: K wl_shm pool mappings pass, the one after
//                     fails as mmap does without address space
//   sigbus-record=N   wayland: the next N SIGBUS guard records the main
//                     thread makes for its shm access fail to allocate (the
//                     renderer's copy lane keeps its own, untouched)
//   prime-import=E    wayland: the next dma-buf plane the driver is asked
//                     to import fails with errno E (13 EACCES: a card fd
//                     that cannot judge; 22 EINVAL: a buffer it refuses)
//   entropy=N         wayland: the next N getrandom reads for activation
//                     tokens find no entropy yet (EAGAIN, as early at boot)
//   security-accept=N wayland: the next N sandboxed connections a security
//                     context accepts are aborted (ECONNABORTED)
//   scanout=K         K Vulkan calls behind KMS scanout buffers pass, the
//                     one after fails
//   scanout-modifier=N the next N scanout modifier queries come back
//                     unsupported
//   vt-state=N        the next N VT_GETSTATE queries fail
//   vt-open=N         the next N opens of the session's VT fail
//   lease=N           wayland drm-lease: the next N lease creations fail
//                     with EBUSY, as when another lessee holds the objects
//   client-import=K   renderer: K client-buffer import calls pass, every
//                     later one fails: the device refuses the client's buffers
//   client-texture=K  K client-sized allocation calls pass, the one after
//                     fails
//   frame-fence=K     K finished-frame fence results pass, the one after
//                     reports a lost device
//   frame-hang=K      the same, reporting the wait timing out instead
//   readback-fence=K  K readback fences pass, the one after reports a lost
//                     device
//   readback-busy=N   fence polls: the next N polls of a readback fence
//                     find it still busy
//   shot-file=K       K steps building the screenshot's file (its memfd,
//                     then each write) pass, the one after fails: the
//                     memfd with EMFILE, a write with ENOSPC
//   descriptor-pool=K K texture descriptor pool creations pass, the one
//                     after runs out of device memory
//   descriptor-set=K  K texture descriptor set allocations pass, every
//                     later one runs out of device memory
//   sync-file=K       K sync-file exports pass, the one after fails
//   sync-wait=K       K sync-file semaphore creations and imports pass, the
//                     one after fails
//   output-target=K   K output-target calls pass, the one after runs out
//                     of device memory
//   no-ext=NAME       the Vulkan device does not offer extension NAME (the
//                     word may repeat, one extension each)
//   frame-submit=N    renderer submits: the next N frame submits are refused
//   readback-submit=N the next N screenshot readback submits are refused
//   capture-submit=N  the next N frame-capture copy submits are refused
//   cursor-submit=N   the next N cursor shape rasterize submits are refused
//   setup=K           K boot-time setup calls pass, the one after runs out
//                     of device memory
// renderer: wl_shm imports and the screenshot capture
//   pool-memory=M     every wl_shm pool import (host pointer, udmabuf
//                     buffer) finds the device's memory types changed:
//                     M=incoherent, none of them is host-coherent; M=none,
//                     there are none at all
//   shot-submit=N     the next N screenshot capture submits are refused
//   udmabuf-read=N    the next N udmabuf read brackets of wl_shm pools are
//                     refused with EIO
//   gpu-wait=K        K waits the renderer cannot go on without pass, the
//                     one after reports a lost device
// the screenshot viewer, a process of its own with its own monkey:
//   swapchain=K       K swapchain acquires and presents pass, the one after
//                     reports the swapchain out of date
//   swapchain-suboptimal=K  the same, reporting it suboptimal instead
// the buses, each word arming one fault on calls to the named D-Bus member;
// MEMBER@K lets K matching calls through first:
//   dbus-message=M    the next message built for M fails to allocate
//   dbus-send=M       the next call to M is not sent and gets no pending
//                     call, as on a connection without memory or dropped
//   dbus-notify=M     the next call to M cannot install its reply notify
//   dbus-sndbuf=N     every bus connection's socket gets an N byte send
//                     buffer (the kernel's floor applies), so long writes
//                     go out in parts
//   dbus-recv-limit=N every bus connection holds at most N bytes of
//                     undispatched messages before it stops reading
// spawn:
//   dev-null=N        the next N opens of /dev/null fail with ENOENT, as
//                     in a chroot or container without one
// renderer: texture descriptor pools
//   descriptor-full=N the chain's first N pools are full: every set
//                     allocation from them runs out of pool memory
//   descriptor-fragmented=N  the first N pools not full are fragmented:
//                     every set allocation from them fails as fragmented
// device: /dev/udmabuf
//   udmabuf-open=N    the next N opens of /dev/udmabuf fail with EACCES, as
//                     for a session not let at it
// screenshot viewer: its encoders
//   encoder-alloc=K   K encoder allocations pass, the one after fails as
//                     without memory
// the millisecond clock:
//   clock-ms=V        the clock reads V at its first reading and runs on
//                     from there: V just under 2^32 wraps it round to zero
//                     early in the session
namespace {
    // buses: one armed fault
    enum class BusFault {
        message,
        pending,
        notify,
    };

    struct BusRule {
        BusFault kind = BusFault::message;
        char member[64] = "";
        int skip = 0;
        bool armed = false;
    };

    struct TestChaosMonkey: public ChaosMonkey {
        int accountFaults = 0;
        // renderer: queue submits
        int frameSubmitFaults = 0;
        int readbackSubmitFaults = 0;
        int captureSubmitFaults = 0;
        int cursorSubmitFaults = 0;
        bool messageArmed = false;
        int messageStyle = 0;
        int responseFaults = 0;
        int answerFaults = 0;
        int memoryFaults = 0;
        int vulkanSkip = -1;
        Vector<StringView> resourceFaults;
        // wayland shm and linux-dmabuf
        int shmMapSkip = -1;
        int sigbusRecordFaults = 0;
        int primeImportErrno = 0;
        int entropyFaults = 0;
        int securityAcceptFaults = 0;
        // KMS backend
        int scanoutSkip = -1;
        int modifierFaults = 0;
        int vtStateFaults = 0;
        int vtOpenFaults = 0;
        int leaseFaults = 0;
        // renderer
        int clientImportSkip = -1;
        int clientTextureSkip = -1;
        int frameFenceSkip = -1;
        VkResult frameFenceFault = VK_SUCCESS;
        int readbackFenceSkip = -1;
        // fence polls
        int readbackBusyPolls = 0;
        // the screenshot's file
        int shotFileSkip = -1;
        int descriptorPoolSkip = -1;
        int descriptorSetSkip = -1;
        int syncFileSkip = -1;
        int syncWaitSkip = -1;
        int outputTargetSkip = -1;
        Vector<StringView> hiddenExtensions;
        int setupSkip = -1;
        // renderer: wl_shm imports and the screenshot capture
        StringView poolMemory;
        int shotSubmitFaults = 0;
        int udmabufReadFaults = 0;
        int gpuWaitSkip = -1;
        // screenshot viewer
        int swapchainSkip = -1;
        VkResult swapchainFault = VK_SUCCESS;
        // buses
        BusRule busRules[8];
        int busSendBuffer = 0;
        long busReceiveLimit = -1;
        // spawn
        int devNullFaults = 0;
        // renderer: texture descriptor pools
        size_t fullPools = 0;
        size_t fragmentedPools = 0;
        // device: /dev/udmabuf
        int udmabufOpenFaults = 0;
        // screenshot viewer: its encoders
        int encoderAllocSkip = -1;
        // the millisecond clock: the offset is fixed at the first reading
        bool clockSet = false;
        u32 clockStart = 0;
        bool clockOffsetKnown = false;
        u32 clockOffset = 0;
#if __has_include(<security/pam_appl.h>)
        pam_message rewritten{};
#endif

        TestChaosMonkey(StringView script);

        passwd* account(passwd* found) override;
        const pam_message* pamMessage(const pam_message* message) override;
        pam_response* pamResponses(pam_response* responses) override;
        char* pamAnswer(char* answer) override;
        void memoryTypes(VkPhysicalDeviceMemoryProperties& props) override;
        VkResult vulkan(VkResult result) override;
        wl_resource* resource(wl_resource* created) override;
        // wayland shm and linux-dmabuf
        void* shmMap(void* mapped, size_t size) override;
        void* sigbusRecord(void* allocated) override;
        int primeImport(int result) override;
        long entropy(long got) override;
        int securityAccept(int fd) override;
        // KMS backend
        VkResult scanout(VkResult result) override;
        VkResult scanoutModifier(VkResult result) override;
        int vtState(int result) override;
        int vtOpen(int fd) override;
        int leaseFd(int fd) override;
        // renderer
        VkResult clientImport(VkResult result) override;
        VkResult clientTexture(VkResult result) override;
        VkResult frameFence(VkResult result) override;
        VkResult readbackFence(VkResult result) override;
        // fence polls
        VkResult readbackPoll(VkResult status) override;
        // the screenshot's file
        int shotFile(int fd) override;
        ssize_t shotWrite(ssize_t written) override;
        VkResult descriptorPool(VkResult result) override;
        VkResult descriptorSet(VkResult result) override;
        int syncFile(int fd) override;
        VkResult syncWait(VkResult result) override;
        VkResult outputTarget(VkResult result) override;
        bool deviceExtension(const char* name, bool offered) override;
        // renderer: queue submits
        VkResult frameSubmit(VkResult pending) override;
        VkResult readbackSubmit(VkResult pending) override;
        VkResult captureSubmit(VkResult pending) override;
        VkResult cursorSubmit(VkResult pending) override;
        VkResult setup(VkResult result) override;
        // renderer: wl_shm imports and the screenshot capture
        void poolMemoryTypes(VkPhysicalDeviceMemoryProperties& props) override;
        VkResult shotSubmit(VkResult pending) override;
        bool udmabufRead(bool started) override;
        VkResult gpuWait(VkResult result) override;
        // screenshot viewer
        VkResult swapchain(VkResult result) override;
        // buses
        DBusMessage* dbusMessage(DBusMessage* built) override;
        DBusMessage* dbusSend(DBusMessage* call) override;
        bool dbusNotify(DBusMessage* sent, bool installed) override;
        void dbusConnection(DBusConnection* conn) override;
        // spawn
        int devNull(int fd) override;
        // renderer: texture descriptor pools
        VkResult descriptorRoom(VkResult result, size_t pool) override;
        // device: /dev/udmabuf
        int udmabufOpen(int fd) override;
        // screenshot viewer: its encoders
        bool encoderAlloc(bool pending) override;
        // the millisecond clock
        u32 clockMs(u32 ms) override;

        void arm(StringView fault, StringView arg);
        void armBus(BusFault kind, StringView arg);
        bool busFires(BusFault kind, DBusMessage* msg);
    };

    static bool spend(int& count) {
        if (count <= 0) {
            return false;
        }

        count--;

        return true;
    }
}

TestChaosMonkey::TestChaosMonkey(StringView script) {
    while (!script.empty()) {
        StringView word, rest, fault, arg;

        if (script.split(' ', word, rest)) {
            script = rest;
        } else {
            word = script;
            script = {};
        }

        if (word.split('=', fault, arg)) {
            arm(fault, arg);
        }
    }
}

void TestChaosMonkey::arm(StringView fault, StringView arg) {
    if (fault == "account"_sv) {
        accountFaults = (int)arg.stou();
    } else if (fault == "pam-message"_sv) {
        messageArmed = true;
        messageStyle = arg == "drop"_sv ? -1 : (int)arg.stou();
    } else if (fault == "pam-responses"_sv) {
        responseFaults = (int)arg.stou();
    } else if (fault == "pam-answer"_sv) {
        answerFaults = (int)arg.stou();
    } else if (fault == "memory-types"_sv) {
        memoryFaults = (int)arg.stou();
    } else if (fault == "vulkan"_sv) {
        vulkanSkip = (int)arg.stou();
    } else if (fault == "resource"_sv) {
        resourceFaults.pushBack(arg);
    } else if (fault == "scanout"_sv) {
        // KMS backend
        scanoutSkip = (int)arg.stou();
    } else if (fault == "scanout-modifier"_sv) {
        // KMS backend
        modifierFaults = (int)arg.stou();
    } else if (fault == "vt-state"_sv) {
        // KMS backend
        vtStateFaults = (int)arg.stou();
    } else if (fault == "vt-open"_sv) {
        // KMS backend
        vtOpenFaults = (int)arg.stou();
    } else if (fault == "lease"_sv) {
        leaseFaults = (int)arg.stou();
    } else if (fault == "client-import"_sv) {
        clientImportSkip = (int)arg.stou();
    } else if (fault == "client-texture"_sv) {
        clientTextureSkip = (int)arg.stou();
    } else if (fault == "frame-fence"_sv || fault == "frame-hang"_sv) {
        frameFenceSkip = (int)arg.stou();
        frameFenceFault = fault == "frame-hang"_sv ? VK_TIMEOUT : VK_ERROR_DEVICE_LOST;
    } else if (fault == "readback-fence"_sv) {
        readbackFenceSkip = (int)arg.stou();
    } else if (fault == "readback-busy"_sv) {
        readbackBusyPolls = (int)arg.stou();
    } else if (fault == "shot-file"_sv) {
        shotFileSkip = (int)arg.stou();
    } else if (fault == "descriptor-pool"_sv) {
        descriptorPoolSkip = (int)arg.stou();
    } else if (fault == "descriptor-set"_sv) {
        descriptorSetSkip = (int)arg.stou();
    } else if (fault == "sync-file"_sv) {
        syncFileSkip = (int)arg.stou();
    } else if (fault == "sync-wait"_sv) {
        syncWaitSkip = (int)arg.stou();
    } else if (fault == "output-target"_sv) {
        outputTargetSkip = (int)arg.stou();
    } else if (fault == "no-ext"_sv) {
        hiddenExtensions.pushBack(arg);
    } else if (fault == "frame-submit"_sv) {
        // renderer: queue submits
        frameSubmitFaults = (int)arg.stou();
    } else if (fault == "readback-submit"_sv) {
        readbackSubmitFaults = (int)arg.stou();
    } else if (fault == "capture-submit"_sv) {
        captureSubmitFaults = (int)arg.stou();
    } else if (fault == "cursor-submit"_sv) {
        cursorSubmitFaults = (int)arg.stou();
    } else if (fault == "setup"_sv) {
        setupSkip = (int)arg.stou();
    } else if (fault == "pool-memory"_sv) {
        // renderer: wl_shm imports and the screenshot capture
        poolMemory = arg;
    } else if (fault == "shot-submit"_sv) {
        shotSubmitFaults = (int)arg.stou();
    } else if (fault == "udmabuf-read"_sv) {
        udmabufReadFaults = (int)arg.stou();
    } else if (fault == "gpu-wait"_sv) {
        gpuWaitSkip = (int)arg.stou();
    } else if (fault == "swapchain"_sv || fault == "swapchain-suboptimal"_sv) {
        swapchainSkip = (int)arg.stou();
        swapchainFault = fault == "swapchain"_sv ? VK_ERROR_OUT_OF_DATE_KHR : VK_SUBOPTIMAL_KHR;
    } else if (fault == "dbus-message"_sv) {
        // buses
        armBus(BusFault::message, arg);
    } else if (fault == "dbus-send"_sv) {
        armBus(BusFault::pending, arg);
    } else if (fault == "dbus-notify"_sv) {
        armBus(BusFault::notify, arg);
    } else if (fault == "dbus-sndbuf"_sv) {
        busSendBuffer = (int)arg.stou();
    } else if (fault == "dbus-recv-limit"_sv) {
        busReceiveLimit = (long)arg.stou();
    } else if (fault == "dev-null"_sv) {
        // spawn
        devNullFaults = (int)arg.stou();
    } else if (fault == "shm-map"_sv) {
        shmMapSkip = (int)arg.stou();
    } else if (fault == "sigbus-record"_sv) {
        sigbusRecordFaults = (int)arg.stou();
    } else if (fault == "prime-import"_sv) {
        primeImportErrno = (int)arg.stou();
    } else if (fault == "entropy"_sv) {
        entropyFaults = (int)arg.stou();
    } else if (fault == "security-accept"_sv) {
        securityAcceptFaults = (int)arg.stou();
    } else if (fault == "descriptor-full"_sv) {
        // renderer: texture descriptor pools
        fullPools = arg.stou();
    } else if (fault == "descriptor-fragmented"_sv) {
        fragmentedPools = arg.stou();
    } else if (fault == "udmabuf-open"_sv) {
        // device: /dev/udmabuf
        udmabufOpenFaults = (int)arg.stou();
    } else if (fault == "encoder-alloc"_sv) {
        // screenshot viewer: its encoders
        encoderAllocSkip = (int)arg.stou();
    } else if (fault == "clock-ms"_sv) {
        // the millisecond clock
        clockSet = true;
        clockStart = (u32)arg.stou();
    }
}

// wayland shm and linux-dmabuf
void* TestChaosMonkey::shmMap(void* mapped, size_t size) {
    if (shmMapSkip < 0 || mapped == MAP_FAILED) {
        return mapped;
    }

    if (shmMapSkip-- > 0) {
        return mapped;
    }

    munmap(mapped, size);

    return MAP_FAILED;
}

void* TestChaosMonkey::sigbusRecord(void* allocated) {
    if (!allocated || gettid() != getpid() || !spend(sigbusRecordFaults)) {
        return allocated;
    }

    free(allocated);

    return nullptr;
}

int TestChaosMonkey::primeImport(int result) {
    if (!primeImportErrno || result != 0) {
        return result;
    }

    errno = primeImportErrno;
    primeImportErrno = 0;

    return -1;
}

long TestChaosMonkey::entropy(long got) {
    if (!spend(entropyFaults)) {
        return got;
    }

    errno = EAGAIN;

    return -1;
}

int TestChaosMonkey::securityAccept(int fd) {
    if (fd < 0 || !spend(securityAcceptFaults)) {
        return fd;
    }

    close(fd);
    errno = ECONNABORTED;

    return -1;
}

void TestChaosMonkey::armBus(BusFault kind, StringView arg) {
    StringView member, skip;

    if (!arg.split('@', member, skip)) {
        member = arg;
        skip = {};
    }

    for (BusRule& rule : busRules) {
        if (!rule.armed) {
            size_t length = member.length() < sizeof(rule.member) - 1 ? member.length() : sizeof(rule.member) - 1;

            memcpy(rule.member, member.data(), length);
            rule.member[length] = 0;
            rule.kind = kind;
            rule.skip = skip.empty() ? 0 : (int)skip.stou();
            rule.armed = true;

            return;
        }
    }
}

// every armed rule for this member counts the call; the first one due
// fires and is spent
// every message the seams see is a method call or a signal, which always
// carries a member
bool TestChaosMonkey::busFires(BusFault kind, DBusMessage* msg) {
    const char* member = dbus_message_get_member(msg);
    bool fire = false;

    for (BusRule& rule : busRules) {
        if (!rule.armed || rule.kind != kind || StringView((const char*)rule.member) != StringView(member)) {
            continue;
        }

        if (rule.skip > 0) {
            rule.skip--;
        } else if (!fire) {
            rule.armed = false;
            fire = true;
        }
    }

    return fire;
}

passwd* TestChaosMonkey::account(passwd* found) {
    return spend(accountFaults) ? nullptr : found;
}

const pam_message* TestChaosMonkey::pamMessage(const pam_message* message) {
    if (!messageArmed) {
        return message;
    }

    messageArmed = false;

    if (messageStyle < 0) {
        return nullptr;
    }

#if __has_include(<security/pam_appl.h>)
    rewritten = *message;
    rewritten.msg_style = messageStyle;

    return &rewritten;
#else
    return message;
#endif
}

pam_response* TestChaosMonkey::pamResponses(pam_response* responses) {
    if (!spend(responseFaults)) {
        return responses;
    }

    free(responses);

    return nullptr;
}

char* TestChaosMonkey::pamAnswer(char* answer) {
    if (!spend(answerFaults)) {
        return answer;
    }

    free(answer);

    return nullptr;
}

void TestChaosMonkey::memoryTypes(VkPhysicalDeviceMemoryProperties& props) {
    if (spend(memoryFaults)) {
        props.memoryTypeCount = 0;
    }
}

VkResult TestChaosMonkey::vulkan(VkResult result) {
    if (vulkanSkip < 0) {
        return result;
    }

    if (vulkanSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

wl_resource* TestChaosMonkey::resource(wl_resource* created) {
    if (!created) {
        return created;
    }

    StringView name(wl_resource_get_class(created));

    for (size_t i = 0; i < resourceFaults.length(); i++) {
        if (resourceFaults[i] == name) {
            resourceFaults.mut(i) = {};
            wl_resource_destroy(created);

            return nullptr;
        }
    }

    return created;
}

// KMS backend
VkResult TestChaosMonkey::scanout(VkResult result) {
    if (scanoutSkip < 0) {
        return result;
    }

    if (scanoutSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult TestChaosMonkey::scanoutModifier(VkResult result) {
    return spend(modifierFaults) ? VK_ERROR_FORMAT_NOT_SUPPORTED : result;
}

int TestChaosMonkey::vtState(int result) {
    return spend(vtStateFaults) ? -1 : result;
}

int TestChaosMonkey::vtOpen(int fd) {
    if (!spend(vtOpenFaults)) {
        return fd;
    }

    if (fd >= 0) {
        close(fd);
    }

    errno = EACCES;

    return -1;
}

int TestChaosMonkey::leaseFd(int fd) {
    if (!spend(leaseFaults)) {
        return fd;
    }

    if (fd >= 0) {
        close(fd);
    }

    return -EBUSY;
}

// renderer
VkResult TestChaosMonkey::clientImport(VkResult result) {
    if (clientImportSkip < 0) {
        return result;
    }

    if (clientImportSkip > 0) {
        clientImportSkip--;

        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult TestChaosMonkey::clientTexture(VkResult result) {
    if (clientTextureSkip < 0) {
        return result;
    }

    if (clientTextureSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult TestChaosMonkey::frameFence(VkResult result) {
    if (frameFenceSkip < 0) {
        return result;
    }

    if (frameFenceSkip-- > 0) {
        return result;
    }

    return frameFenceFault;
}

VkResult TestChaosMonkey::readbackFence(VkResult result) {
    if (readbackFenceSkip < 0) {
        return result;
    }

    if (readbackFenceSkip-- > 0) {
        return result;
    }

    return VK_ERROR_DEVICE_LOST;
}

// fence polls
VkResult TestChaosMonkey::readbackPoll(VkResult status) {
    return spend(readbackBusyPolls) ? VK_NOT_READY : status;
}

// the screenshot's file
int TestChaosMonkey::shotFile(int fd) {
    if (shotFileSkip < 0 || shotFileSkip-- > 0) {
        return fd;
    }

    if (fd >= 0) {
        close(fd);
    }

    errno = EMFILE;

    return -1;
}

ssize_t TestChaosMonkey::shotWrite(ssize_t written) {
    if (shotFileSkip < 0 || shotFileSkip-- > 0) {
        return written;
    }

    errno = ENOSPC;

    return -1;
}

VkResult TestChaosMonkey::descriptorPool(VkResult result) {
    if (descriptorPoolSkip < 0) {
        return result;
    }

    if (descriptorPoolSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult TestChaosMonkey::descriptorSet(VkResult result) {
    if (descriptorSetSkip < 0) {
        return result;
    }

    if (descriptorSetSkip > 0) {
        descriptorSetSkip--;

        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

int TestChaosMonkey::syncFile(int fd) {
    if (syncFileSkip < 0 || syncFileSkip-- > 0) {
        return fd;
    }

    close(fd);

    return -1;
}

VkResult TestChaosMonkey::syncWait(VkResult result) {
    if (syncWaitSkip < 0 || syncWaitSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult TestChaosMonkey::outputTarget(VkResult result) {
    if (outputTargetSkip < 0 || outputTargetSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult TestChaosMonkey::setup(VkResult result) {
    if (setupSkip < 0 || setupSkip-- > 0) {
        return result;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

// renderer: wl_shm imports and the screenshot capture
void TestChaosMonkey::poolMemoryTypes(VkPhysicalDeviceMemoryProperties& props) {
    if (poolMemory == "none"_sv) {
        props.memoryTypeCount = 0;
    } else if (poolMemory == "incoherent"_sv) {
        for (u32 i = 0; i < props.memoryTypeCount; i++) {
            props.memoryTypes[i].propertyFlags &= ~(VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
    }
}

VkResult TestChaosMonkey::shotSubmit(VkResult pending) {
    return spend(shotSubmitFaults) ? VK_ERROR_OUT_OF_DEVICE_MEMORY : pending;
}

bool TestChaosMonkey::udmabufRead(bool started) {
    if (!started || !spend(udmabufReadFaults)) {
        return started;
    }

    errno = EIO;

    return false;
}

VkResult TestChaosMonkey::gpuWait(VkResult result) {
    if (gpuWaitSkip < 0 || gpuWaitSkip-- > 0) {
        return result;
    }

    return VK_ERROR_DEVICE_LOST;
}

// screenshot viewer
VkResult TestChaosMonkey::swapchain(VkResult result) {
    if (swapchainSkip < 0 || swapchainSkip-- > 0) {
        return result;
    }

    return swapchainFault;
}

bool TestChaosMonkey::deviceExtension(const char* name, bool offered) {
    for (StringView hidden : hiddenExtensions) {
        if (hidden == StringView(name)) {
            return false;
        }
    }

    return offered;
}

// renderer: queue submits
VkResult TestChaosMonkey::frameSubmit(VkResult pending) {
    return spend(frameSubmitFaults) ? VK_ERROR_DEVICE_LOST : pending;
}

VkResult TestChaosMonkey::readbackSubmit(VkResult pending) {
    return spend(readbackSubmitFaults) ? VK_ERROR_OUT_OF_DEVICE_MEMORY : pending;
}

VkResult TestChaosMonkey::captureSubmit(VkResult pending) {
    return spend(captureSubmitFaults) ? VK_ERROR_OUT_OF_DEVICE_MEMORY : pending;
}

VkResult TestChaosMonkey::cursorSubmit(VkResult pending) {
    return spend(cursorSubmitFaults) ? VK_ERROR_OUT_OF_DEVICE_MEMORY : pending;
}

// buses
// a site's own allocation may already have failed
DBusMessage* TestChaosMonkey::dbusMessage(DBusMessage* built) {
    if (!built || !busFires(BusFault::message, built)) {
        return built;
    }

    dbus_message_unref(built);

    return nullptr;
}

DBusMessage* TestChaosMonkey::dbusSend(DBusMessage* call) {
    return busFires(BusFault::pending, call) ? nullptr : call;
}

bool TestChaosMonkey::dbusNotify(DBusMessage* sent, bool installed) {
    return installed && !busFires(BusFault::notify, sent);
}

void TestChaosMonkey::dbusConnection(DBusConnection* conn) {
    int fd = -1;

    if (busSendBuffer > 0 && dbus_connection_get_socket(conn, &fd)) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &busSendBuffer, sizeof(busSendBuffer));
    }

    if (busReceiveLimit >= 0) {
        dbus_connection_set_max_received_size(conn, busReceiveLimit);
    }
}

// spawn
int TestChaosMonkey::devNull(int fd) {
    if (!spend(devNullFaults)) {
        return fd;
    }

    // a real failure already left -1, which close shrugs off
    close(fd);
    errno = ENOENT;

    return -1;
}

// renderer: texture descriptor pools
VkResult TestChaosMonkey::descriptorRoom(VkResult result, size_t pool) {
    // a set the pool did hand out stays in it until the pool goes
    if (pool < fullPools) {
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }

    if (pool < fullPools + fragmentedPools) {
        return VK_ERROR_FRAGMENTED_POOL;
    }

    return result;
}

// device: /dev/udmabuf
int TestChaosMonkey::udmabufOpen(int fd) {
    if (!spend(udmabufOpenFaults)) {
        return fd;
    }

    // a real failure already left -1, which close shrugs off
    close(fd);
    errno = EACCES;

    return -1;
}

// screenshot viewer: its encoders
bool TestChaosMonkey::encoderAlloc(bool pending) {
    if (encoderAllocSkip < 0 || encoderAllocSkip-- > 0) {
        return pending;
    }

    return false;
}

// the millisecond clock: unsigned arithmetic wraps the shifted clock
// round zero exactly as the real one does
u32 TestChaosMonkey::clockMs(u32 ms) {
    if (!clockSet) {
        return ms;
    }

    if (!clockOffsetKnown) {
        clockOffset = clockStart - ms;
        clockOffsetKnown = true;
    }

    return ms + clockOffset;
}

ChaosMonkey* ChaosMonkey::create(ObjPool& pool) {
    const char* script = getenv("IMWAY_CHAOS");

    return pool.make<TestChaosMonkey>(StringView(script ? script : ""));
}
#else
// The production monkey: every call gives its argument back.
namespace {
    struct IdleChaosMonkey: public ChaosMonkey {
        passwd* account(passwd* found) override;
        const pam_message* pamMessage(const pam_message* message) override;
        pam_response* pamResponses(pam_response* responses) override;
        char* pamAnswer(char* answer) override;
        void memoryTypes(VkPhysicalDeviceMemoryProperties& props) override;
        VkResult vulkan(VkResult result) override;
        wl_resource* resource(wl_resource* created) override;
        // wayland shm and linux-dmabuf
        void* shmMap(void* mapped, size_t size) override;
        void* sigbusRecord(void* allocated) override;
        int primeImport(int result) override;
        long entropy(long got) override;
        int securityAccept(int fd) override;
        // KMS backend
        VkResult scanout(VkResult result) override;
        VkResult scanoutModifier(VkResult result) override;
        int vtState(int result) override;
        int vtOpen(int fd) override;
        int leaseFd(int fd) override;
        // renderer
        VkResult clientImport(VkResult result) override;
        VkResult clientTexture(VkResult result) override;
        VkResult frameFence(VkResult result) override;
        VkResult readbackFence(VkResult result) override;
        // fence polls
        VkResult readbackPoll(VkResult status) override;
        // the screenshot's file
        int shotFile(int fd) override;
        ssize_t shotWrite(ssize_t written) override;
        VkResult descriptorPool(VkResult result) override;
        VkResult descriptorSet(VkResult result) override;
        int syncFile(int fd) override;
        VkResult syncWait(VkResult result) override;
        VkResult outputTarget(VkResult result) override;
        bool deviceExtension(const char* name, bool offered) override;
        // renderer: queue submits
        VkResult frameSubmit(VkResult pending) override;
        VkResult readbackSubmit(VkResult pending) override;
        VkResult captureSubmit(VkResult pending) override;
        VkResult cursorSubmit(VkResult pending) override;
        VkResult setup(VkResult result) override;
        // renderer: wl_shm imports and the screenshot capture
        void poolMemoryTypes(VkPhysicalDeviceMemoryProperties& props) override;
        VkResult shotSubmit(VkResult pending) override;
        bool udmabufRead(bool started) override;
        VkResult gpuWait(VkResult result) override;
        // screenshot viewer
        VkResult swapchain(VkResult result) override;
        // buses
        DBusMessage* dbusMessage(DBusMessage* built) override;
        DBusMessage* dbusSend(DBusMessage* call) override;
        bool dbusNotify(DBusMessage* sent, bool installed) override;
        void dbusConnection(DBusConnection* conn) override;
        // spawn
        int devNull(int fd) override;
        // renderer: texture descriptor pools
        VkResult descriptorRoom(VkResult result, size_t pool) override;
        // device: /dev/udmabuf
        int udmabufOpen(int fd) override;
        // screenshot viewer: its encoders
        bool encoderAlloc(bool pending) override;
        // the millisecond clock
        u32 clockMs(u32 ms) override;
    };
}

passwd* IdleChaosMonkey::account(passwd* found) {
    return found;
}

const pam_message* IdleChaosMonkey::pamMessage(const pam_message* message) {
    return message;
}

pam_response* IdleChaosMonkey::pamResponses(pam_response* responses) {
    return responses;
}

char* IdleChaosMonkey::pamAnswer(char* answer) {
    return answer;
}

void IdleChaosMonkey::memoryTypes(VkPhysicalDeviceMemoryProperties&) {
}

VkResult IdleChaosMonkey::vulkan(VkResult result) {
    return result;
}

wl_resource* IdleChaosMonkey::resource(wl_resource* created) {
    return created;
}

// wayland shm and linux-dmabuf
void* IdleChaosMonkey::shmMap(void* mapped, size_t) {
    return mapped;
}

void* IdleChaosMonkey::sigbusRecord(void* allocated) {
    return allocated;
}

int IdleChaosMonkey::primeImport(int result) {
    return result;
}

long IdleChaosMonkey::entropy(long got) {
    return got;
}

int IdleChaosMonkey::securityAccept(int fd) {
    return fd;
}

// KMS backend
VkResult IdleChaosMonkey::scanout(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::scanoutModifier(VkResult result) {
    return result;
}

int IdleChaosMonkey::vtState(int result) {
    return result;
}

int IdleChaosMonkey::vtOpen(int fd) {
    return fd;
}

int IdleChaosMonkey::leaseFd(int fd) {
    return fd;
}

// renderer
VkResult IdleChaosMonkey::clientImport(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::clientTexture(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::frameFence(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::readbackFence(VkResult result) {
    return result;
}

// fence polls
VkResult IdleChaosMonkey::readbackPoll(VkResult status) {
    return status;
}

// the screenshot's file
int IdleChaosMonkey::shotFile(int fd) {
    return fd;
}

ssize_t IdleChaosMonkey::shotWrite(ssize_t written) {
    return written;
}

VkResult IdleChaosMonkey::descriptorPool(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::descriptorSet(VkResult result) {
    return result;
}

int IdleChaosMonkey::syncFile(int fd) {
    return fd;
}

VkResult IdleChaosMonkey::syncWait(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::outputTarget(VkResult result) {
    return result;
}

VkResult IdleChaosMonkey::setup(VkResult result) {
    return result;
}

// renderer: wl_shm imports and the screenshot capture
void IdleChaosMonkey::poolMemoryTypes(VkPhysicalDeviceMemoryProperties&) {
}

VkResult IdleChaosMonkey::shotSubmit(VkResult pending) {
    return pending;
}

bool IdleChaosMonkey::udmabufRead(bool started) {
    return started;
}

VkResult IdleChaosMonkey::gpuWait(VkResult result) {
    return result;
}

// screenshot viewer
VkResult IdleChaosMonkey::swapchain(VkResult result) {
    return result;
}

bool IdleChaosMonkey::deviceExtension(const char*, bool offered) {
    return offered;
}

// renderer: queue submits
VkResult IdleChaosMonkey::frameSubmit(VkResult pending) {
    return pending;
}

VkResult IdleChaosMonkey::readbackSubmit(VkResult pending) {
    return pending;
}

VkResult IdleChaosMonkey::captureSubmit(VkResult pending) {
    return pending;
}

VkResult IdleChaosMonkey::cursorSubmit(VkResult pending) {
    return pending;
}

// buses
DBusMessage* IdleChaosMonkey::dbusMessage(DBusMessage* built) {
    return built;
}

DBusMessage* IdleChaosMonkey::dbusSend(DBusMessage* call) {
    return call;
}

bool IdleChaosMonkey::dbusNotify(DBusMessage*, bool installed) {
    return installed;
}

void IdleChaosMonkey::dbusConnection(DBusConnection*) {
}

// spawn
int IdleChaosMonkey::devNull(int fd) {
    return fd;
}

// renderer: texture descriptor pools
VkResult IdleChaosMonkey::descriptorRoom(VkResult result, size_t) {
    return result;
}

// device: /dev/udmabuf
int IdleChaosMonkey::udmabufOpen(int fd) {
    return fd;
}

// screenshot viewer: its encoders
bool IdleChaosMonkey::encoderAlloc(bool pending) {
    return pending;
}

// the millisecond clock
u32 IdleChaosMonkey::clockMs(u32 ms) {
    return ms;
}

ChaosMonkey* ChaosMonkey::create(ObjPool& pool) {
    return pool.make<IdleChaosMonkey>();
}
#endif
