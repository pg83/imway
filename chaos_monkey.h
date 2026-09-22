#pragma once

#include <vulkan/vulkan.h>

namespace stl {
    class ObjPool;
}

struct passwd;
struct wl_resource;
struct pam_message;
struct pam_response;

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

    static ChaosMonkey* create(stl::ObjPool& pool);
};
