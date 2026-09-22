#include "chaos_monkey.h"

#include "util.h"

#include <std/str/view.h>
#include <std/mem/obj_pool.h>

#ifdef IMWAY_FOR_TESTS
    #if __has_include(<security/pam_appl.h>)
        #include <security/pam_appl.h>
    #endif

    #include <stdlib.h>
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
namespace {
    struct TestChaosMonkey: public ChaosMonkey {
        int accountFaults = 0;
        bool messageArmed = false;
        int messageStyle = 0;
        int responseFaults = 0;
        int answerFaults = 0;
        int memoryFaults = 0;
        int vulkanSkip = -1;
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

ChaosMonkey* ChaosMonkey::create(ObjPool& pool) {
    return pool.make<IdleChaosMonkey>();
}
#endif
