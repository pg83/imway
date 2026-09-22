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
    #include <unistd.h>
    #include <std/lib/vector.h>
    #include <wayland-server-core.h>
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
//   scanout=K         K Vulkan calls behind KMS scanout buffers pass, the
//                     one after fails
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
namespace {
    struct TestChaosMonkey: public ChaosMonkey {
        int accountFaults = 0;
        bool messageArmed = false;
        int messageStyle = 0;
        int responseFaults = 0;
        int answerFaults = 0;
        int memoryFaults = 0;
        int vulkanSkip = -1;
        Vector<StringView> resourceFaults;
        // KMS backend
        int scanoutSkip = -1;
        int leaseFaults = 0;
        // renderer
        int clientImportSkip = -1;
        int clientTextureSkip = -1;
        int frameFenceSkip = -1;
        VkResult frameFenceFault = VK_SUCCESS;
        int readbackFenceSkip = -1;
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
        // KMS backend
        VkResult scanout(VkResult result) override;
        int leaseFd(int fd) override;
        // renderer
        VkResult clientImport(VkResult result) override;
        VkResult clientTexture(VkResult result) override;
        VkResult frameFence(VkResult result) override;
        VkResult readbackFence(VkResult result) override;

        void arm(StringView fault, StringView arg);
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
    }
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
        // KMS backend
        VkResult scanout(VkResult result) override;
        int leaseFd(int fd) override;
        // renderer
        VkResult clientImport(VkResult result) override;
        VkResult clientTexture(VkResult result) override;
        VkResult frameFence(VkResult result) override;
        VkResult readbackFence(VkResult result) override;
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

// KMS backend
VkResult IdleChaosMonkey::scanout(VkResult result) {
    return result;
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

ChaosMonkey* ChaosMonkey::create(ObjPool& pool) {
    return pool.make<IdleChaosMonkey>();
}
#endif
