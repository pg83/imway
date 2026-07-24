#include "lock_screen.h"

#include "log.h"
#include "scene.h"
#include "dialog.h"
#include "composer.h"
#include "imgui_wm.h"
#include "tex_pool.h"
#include "device_vk.h"
#include "render_filter.h"

#include <std/ios/sys.h>
#include <std/str/view.h>
#include <std/dbg/verify.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <pwd.h>
#include <crypt.h>
#include <shadow.h>
#include <string.h>
#include <unistd.h>
#include <imgui_impl_vulkan.h>
#include <lock_screen_blur.spv.h>

using namespace stl;

namespace {
    void wipe(char* data, size_t size) noexcept {
        volatile char* p = data;

        while (size--) {
            *p++ = 0;
        }
    }

    void wipeImGuiPasswordState() noexcept {
        ImGuiContext& context = *GImGui;
        ImGuiInputTextState& state = context.InputTextState;

        if (state.TextA.Data) {
            wipe(state.TextA.Data, (size_t)state.TextA.Capacity);
        }
        if (state.TextToRevertTo.Data) {
            wipe(state.TextToRevertTo.Data, (size_t)state.TextToRevertTo.Capacity);
        }
        if (state.CallbackTextBackup.Data) {
            wipe(state.CallbackTextBackup.Data, (size_t)state.CallbackTextBackup.Capacity);
        }
        if (context.InputTextDeactivatedState.TextA.Data) {
            wipe(context.InputTextDeactivatedState.TextA.Data, (size_t)context.InputTextDeactivatedState.TextA.Capacity);
        }

        state.ClearFreeMemory();
        state.ID = 0;
        state.TextLen = 0;
        state.TextSrc = nullptr;
        context.InputTextDeactivatedState.ClearFreeMemory();
        ImGui::ClearActiveID();
        ImGui::GetIO().WantTextInput = false;
        context.PlatformImeData.WantTextInput = false;
        context.PlatformImeData.WantVisible = false;
        context.WantTextInputNextFrame = 0;
    }

    bool secureEqual(StringView a, StringView b) {
        size_t n = a.length() > b.length() ? a.length() : b.length();
        unsigned diff = (unsigned)(a.length() ^ b.length());

        for (size_t i = 0; i < n; i++) {
            u8 ac = i < a.length() ? a[i] : 0;
            u8 bc = i < b.length() ? b[i] : 0;

            diff |= ac ^ bc;
        }

        return diff == 0;
    }

    bool authenticate(StringView password) {
        // Temporary development escape hatch. PAM replaces the shadow path,
        // not this deliberately conspicuous first-stage exception.
        if (secureEqual(password, StringView("xxx"))) {
            return true;
        }

        if (password.empty()) {
            return false;
        }

        passwd* account = getpwuid(getuid());

        if (!account || !account->pw_name || !account->pw_passwd) {
            return false;
        }

        const char* hash = account->pw_passwd;

        if (!strcmp(hash, "x")) {
            spwd* shadow = getspnam(account->pw_name);

            if (!shadow || !shadow->sp_pwdp) {
                return false;
            }

            hash = shadow->sp_pwdp;
        }

        if (!hash[0] || hash[0] == '!' || hash[0] == '*') {
            return false;
        }

        Buffer input(password);
        char* encoded = crypt(input.cStr(), hash);
        bool ok = encoded && secureEqual(StringView(encoded), StringView(hash));

        wipe((char*)input.data(), input.length());

        return ok;
    }

    void initDrawData(ImDrawData& out, const ImDrawData& src) {
        out.Valid = src.Valid;
        out.DisplayPos = src.DisplayPos;
        out.DisplaySize = src.DisplaySize;
        out.FramebufferScale = src.FramebufferScale;
        out.OwnerViewport = src.OwnerViewport;
        out.Textures = src.Textures;
    }

    struct LockFilter: Filter {
        ImDrawList* overlayDrawList = nullptr;
        ImDrawList* foregroundDrawList = nullptr; // software/client cursor, drawn after the dialog

        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkTexturePool* textures = nullptr;
        VkFormat format = VK_FORMAT_UNDEFINED;
        int width = 0, height = 0;
        int blurW = 0, blurH = 0;

        VkImage baseImage = VK_NULL_HANDLE;
        VkDeviceMemory baseMemory = VK_NULL_HANDLE;
        VkImageView baseView = VK_NULL_HANDLE;
        VkRenderPass basePass = VK_NULL_HANDLE;
        VkFramebuffer baseFramebuffer = VK_NULL_HANDLE;
        VkImage blurImages[2] = {};
        VkDeviceMemory blurMemory[2] = {};
        VkImageView blurViews[2] = {};
        VkDescriptorSet blurUi = VK_NULL_HANDLE;
        VkDescriptorPool blurUiPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSets[2] = {};
        bool fresh = true;

        ~LockFilter() noexcept;

        ImTextureID background() const {
            return (ImTextureID)(uintptr_t)blurUi;
        }

        void apply(RenderContext& ctx) override;
        void setup(RenderContext& ctx);
        void recordBlur(VkCommandBuffer commands);
        u32 findMemoryType(u32 bits, VkMemoryPropertyFlags flags) const;
        void createImage(int w, int h, VkFormat fmt, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    };

    struct Dialog {
        LockFilter filter;
        Log* log = nullptr;
        char password[256] = "";
        bool focusField = true;
        bool failed = false;
        bool closeRequested = false;

        ~Dialog() noexcept {
            filter.unlink();
            wipe(password, sizeof(password));
            wipeImGuiPasswordState();
            *log << StringView("imway: lockscreen closed") << endL;
        }

        void draw(Composer& c, bool& open);
    };
}

u32 LockFilter::findMemoryType(u32 bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties props{};

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);

    for (u32 i = 0; i < props.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }

    return UINT32_MAX;
}

void LockFilter::createImage(int w, int h, VkFormat fmt, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {(u32)w, (u32)h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &ici, nullptr, &image));

    VkMemoryRequirements req{};

    vkGetImageMemoryRequirements(device, image, &req);

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &memory));
    VK_CHECK(vkBindImageMemory(device, image, memory, 0));
}

void LockFilter::setup(RenderContext& ctx) {
    physicalDevice = ctx.physicalDevice;
    device = ctx.device;
    sampler = ctx.sampler;
    textures = ctx.textures;
    format = ctx.format;
    width = ctx.width;
    height = ctx.height;
    blurW = width > 4 ? (width + 3) / 4 : 1;
    blurH = height > 4 ? (height + 3) / 4 : 1;

    createImage(width, height, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, baseImage, baseMemory);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = baseImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &baseView));

    VkAttachmentDescription attachment{};

    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};

    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};

    rpci.attachmentCount = 1;
    rpci.pAttachments = &attachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &basePass));

    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

    fci.renderPass = basePass;
    fci.attachmentCount = 1;
    fci.pAttachments = &baseView;
    fci.width = (u32)width;
    fci.height = (u32)height;
    fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &baseFramebuffer));

    for (int i = 0; i < 2; i++) {
        createImage(blurW, blurH, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, blurImages[i], blurMemory[i]);

        vci.image = blurImages[i];
        vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &blurViews[i]));
    }

    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

    smci.codeSize = sizeof(lock_screen_blur_spv);
    smci.pCode = lock_screen_blur_spv;
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &smci, nullptr, &module));

    VkDescriptorSetLayoutBinding bindings[2] = {};

    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    dlci.bindingCount = 2;
    dlci.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &setLayout));

    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(i32)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push;
    VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &pipeLayout));

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = module;
    cpci.stage.pName = "main";
    cpci.layout = pipeLayout;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));
    vkDestroyShaderModule(device, module, nullptr);

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
    };
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};

    dpci.maxSets = 2;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool));

    VkDescriptorSetLayout layouts[2] = {setLayout, setLayout};
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};

    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = 2;
    dsai.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(device, &dsai, descriptorSets));

    for (int i = 0; i < 2; i++) {
        int destination = 1 - i;
        VkDescriptorImageInfo sourceInfo{sampler, blurViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo destinationInfo{VK_NULL_HANDLE, blurViews[destination], VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2] = {};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &sourceInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &destinationInfo;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    blurUi = textures->alloc(blurViews[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, blurUiPool);
    STD_VERIFY(blurUi);
}

LockFilter::~LockFilter() noexcept {
    unlink();

    if (!device) {
        return;
    }

    if (blurUi) {
        textures->free(blurUi, blurUiPool);
    }
    if (descriptorPool) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    if (pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (pipeLayout) {
        vkDestroyPipelineLayout(device, pipeLayout, nullptr);
    }
    if (setLayout) {
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    }
    if (baseFramebuffer) {
        vkDestroyFramebuffer(device, baseFramebuffer, nullptr);
    }
    if (basePass) {
        vkDestroyRenderPass(device, basePass, nullptr);
    }
    for (int i = 0; i < 2; i++) {
        if (blurViews[i]) {
            vkDestroyImageView(device, blurViews[i], nullptr);
        }
        if (blurImages[i]) {
            vkDestroyImage(device, blurImages[i], nullptr);
        }
        if (blurMemory[i]) {
            vkFreeMemory(device, blurMemory[i], nullptr);
        }
    }
    if (baseView) {
        vkDestroyImageView(device, baseView, nullptr);
    }
    if (baseImage) {
        vkDestroyImage(device, baseImage, nullptr);
    }
    if (baseMemory) {
        vkFreeMemory(device, baseMemory, nullptr);
    }
}

void LockFilter::recordBlur(VkCommandBuffer commands) {
    VkImageMemoryBarrier base{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    base.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    base.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    base.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    base.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    base.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    base.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    base.image = baseImage;
    base.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &base);

    VkImageMemoryBarrier down{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    down.srcAccessMask = fresh ? 0 : VK_ACCESS_SHADER_READ_BIT;
    down.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    down.oldLayout = fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    down.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    down.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    down.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    down.image = blurImages[0];
    down.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, fresh ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &down);

    VkImageBlit blit{};

    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {width, height, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {blurW, blurH, 1};
    vkCmdBlitImage(commands, baseImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blurImages[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    VkImageMemoryBarrier firstRead = down;

    firstRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    firstRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    firstRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    firstRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &firstRead);

    VkImageMemoryBarrier secondWrite{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    secondWrite.srcAccessMask = fresh ? 0 : VK_ACCESS_SHADER_READ_BIT;
    secondWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    secondWrite.oldLayout = fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    secondWrite.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    secondWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    secondWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    secondWrite.image = blurImages[1];
    secondWrite.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, fresh ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &secondWrite);

    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout, 0, 1, &descriptorSets[0], 0, nullptr);
    i32 horizontal[2] = {1, 0};
    vkCmdPushConstants(commands, pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(horizontal), horizontal);
    vkCmdDispatch(commands, ((u32)blurW + 7) / 8, ((u32)blurH + 7) / 8, 1);

    VkImageMemoryBarrier secondRead = secondWrite;

    secondRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    secondRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    secondRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    secondRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &secondRead);

    VkImageMemoryBarrier finalWrite = firstRead;

    finalWrite.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalWrite.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalWrite.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &finalWrite);

    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout, 0, 1, &descriptorSets[1], 0, nullptr);
    i32 vertical[2] = {0, 1};
    vkCmdPushConstants(commands, pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(vertical), vertical);
    vkCmdDispatch(commands, ((u32)blurW + 7) / 8, ((u32)blurH + 7) / 8, 1);

    VkImageMemoryBarrier finalRead = finalWrite;

    finalRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    finalRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &finalRead);
    fresh = false;
}

void LockFilter::apply(RenderContext& ctx) {
    if (ctx.handled || !overlayDrawList || !ctx.drawData) {
        return;
    }

    if (!device) {
        setup(ctx);
    }

    ImDrawData baseData, overlayData;

    initDrawData(baseData, *ctx.drawData);
    initDrawData(overlayData, *ctx.drawData);

    for (ImDrawList* list : ctx.drawData->CmdLists) {
        (list == overlayDrawList || list == foregroundDrawList ? overlayData : baseData).AddDrawList(list);
    }

    VkClearValue clear{};

    clear.color = {{ctx.clearColor[0], ctx.clearColor[1], ctx.clearColor[2], ctx.clearColor[3]}};

    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    begin.renderPass = basePass;
    begin.framebuffer = baseFramebuffer;
    begin.renderArea = {{0, 0}, {(u32)width, (u32)height}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(ctx.commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(&baseData, ctx.commands);
    vkCmdEndRenderPass(ctx.commands);

    recordBlur(ctx.commands);

    begin.renderPass = ctx.outputPass;
    begin.framebuffer = ctx.outputFramebuffer;
    vkCmdBeginRenderPass(ctx.commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(&overlayData, ctx.commands);
    vkCmdEndRenderPass(ctx.commands);
    ctx.handled = true;
}

void Dialog::draw(Composer& c, bool& open) {
    (void)open;
    float w = (float)c.scene->outW;
    float h = (float)c.scene->outH;
    float scale = ImGui::GetStyle().FontScaleMain;

    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##lock-overlay", nullptr, flags)) {
        filter.overlayDrawList = ImGui::GetWindowDrawList();
        filter.foregroundDrawList = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());

        ImDrawList* draw = filter.overlayDrawList;
        ImVec2 min = ImGui::GetWindowPos();
        ImVec2 max(min.x + w, min.y + h);

        if (ImTextureID background = filter.background()) {
            draw->AddCallback(ImGui_ImplVulkan_TextureEncodingCallback, (void*)2);
            draw->AddImage(background, min, max);
            draw->AddCallback(ImGui_ImplVulkan_TextureEncodingCallback, nullptr);
        }

        draw->AddRectFilled(min, max, themeColorU32(themeAlpha(c.theme.desktop, 0.42f)));

        float fieldW = 360.f * scale;
        float contentH = 78.f * scale;
        ImVec2 p0((w - fieldW) * 0.5f, (h - contentH) * 0.5f);

        ImGui::SetCursorScreenPos(p0);
        ImGui::TextUnformatted("locked");
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + 28.f * scale));
        ImGui::SetNextItemWidth(fieldW);

        if (focusField) {
            ImGui::SetKeyboardFocusHere();
            focusField = false;

            if (failed) {
                *(c.log) << StringView("imway: lockscreen refocused") << endL;
            }
        }

        bool enter = ImGui::InputText("##password", password, sizeof(password), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

        if (failed) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + 60.f * scale));
            ImGui::TextColored(ImVec4(1.f, 0.42f, 0.42f, 1.f), "wrong password");
        }

        if (enter) {
            bool accepted = authenticate(StringView(password));

            wipe(password, sizeof(password));
            wipeImGuiPasswordState();

            if (accepted) {
                *(c.log) << StringView("imway: lockscreen accepted") << endL;
                closeRequested = true;
            } else {
                *(c.log) << StringView("imway: lockscreen rejected") << endL;
                failed = true;
                focusField = true;
            }
        }
    }

    ImGui::End();
}

void openLockOverlay(Composer& c, DialogState** state) {
    if (*state) {
        return;
    }

    ObjPool* pool = ObjPool::fromMemoryRaw();
    DialogState* created = pool->make<DialogState>();

    created->pool = pool;
    created->opaque = pool->make<Dialog>();
    ((Dialog*)created->opaque)->log = c.log;
    *state = created;
    c.filters.pushBack(&((Dialog*)created->opaque)->filter);

    c.scene->needsFrame = true;
}

void drawLockOverlay(Composer& c, DialogState** state) {
    DialogState*& handle = *state;
    Dialog* current = handle ? (Dialog*)handle->opaque : nullptr;

    // Authentication closed the previous rendered frame. Its GPU submission
    // has retired before the renderer starts another, so the arena and filter
    // resources are safe to release now.
    if (current && current->closeRequested) {
        dialog(handle);

        return;
    }

    if (current) {
        bool open = true;

        current->draw(c, open);
    }
}

void closeLockOverlay(DialogState** state) noexcept {
    dialog(*state);
}
