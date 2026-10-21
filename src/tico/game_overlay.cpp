// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// vk_common.h sets up vulkan-hpp (VK_NO_PROTOTYPES, the dynamic dispatcher) and
// comes before the ImGui Vulkan backend so the backend's <vulkan/vulkan.h> sees it.
#include "video_core/renderer_vulkan/vk_common.h"

#include "tico/game_overlay.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "TicoOverlayHost.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "overlay/imgui_overlay.h"
#include "overlay/translation_manager.h"
#include "video_core/overlay.h"
#include "video_core/renderer_vulkan/vk_present_window.h"

namespace SwitchFrontend::GameOverlay {
namespace {

using OverlayUI::Action;

// Held directions repeat after a while, then every so often. Timed rather than counted: the
// main loop polls every few milliseconds, far more often than frames are drawn.
constexpr auto kNavInitialDelay = std::chrono::milliseconds(300);
constexpr auto kNavRepeat = std::chrono::milliseconds(85);
// A press this soon after the last move is a stick wavering at its edge, not a new press.
constexpr auto kNavDebounce = std::chrono::milliseconds(60);

enum NavBits : unsigned int {
    NavBit_Up = 1u << 0,
    NavBit_Down = 1u << 1,
    NavBit_Left = 1u << 2,
    NavBit_Right = 1u << 3,
    NavBit_Accept = 1u << 4,
    NavBit_Cancel = 1u << 5,
};

// --- shared between the main loop and the present thread ---
std::atomic_bool s_registered = false;
std::atomic_bool s_visible = false;
std::atomic_bool s_cheat_refresh = false;
std::atomic_bool s_resume_prompt = false;
std::atomic_int s_pending_action = 0;
std::atomic_uint s_pending_nav = 0;
std::atomic_bool s_touch_down = false;
std::atomic<float> s_touch_x = 0.0f;
std::atomic<float> s_touch_y = 0.0f;
std::mutex s_notice_mutex;
std::optional<std::pair<std::string, std::vector<std::string>>> s_pending_notice;

// --- main loop only ---
bool s_was_combo_down = false;
u64 s_nav_held_prev = 0;
std::chrono::steady_clock::time_point s_nav_next_repeat;
std::chrono::steady_clock::time_point s_nav_last_fire;
bool s_accept_prev = false;
bool s_cancel_prev = false;

// --- Vulkan objects, made on the first draw from what the present path hands it ---
vk::Instance s_instance;
vk::PhysicalDevice s_physical_device;
vk::Device s_device;
vk::Queue s_queue;
u32 s_queue_family = 0;
u32 s_image_count = 2;
vk::DescriptorPool s_descriptor_pool;
vk::CommandPool s_command_pool;
// Dekopon's lock on the graphics queue: Vulkan wants every submit and wait on it serialized, and
// the scheduler's worker submits from another thread (the GPU thread's frames among them).
std::mutex* s_queue_mutex = nullptr;
// Held while the present thread draws, so Shutdown waits for a draw in progress.
std::mutex s_draw_mutex;

// --- present thread only ---
bool s_ready = false;
bool s_failed = false;
bool s_shown = false;
bool s_backend_ready = false;
std::chrono::steady_clock::time_point s_last_frame;
// how long the "Compiling shaders" notice stays up (present thread only)
std::chrono::steady_clock::time_point s_shader_notice_until;
vk::Format s_color_format = vk::Format::eUndefined;
// ImGui's pipeline is made against the first; every pass is compatible with it, as only the
// layout the swapchain image comes in differs.
vk::RenderPass s_render_pass;
vk::RenderPass s_render_pass_after_overlays;

struct Texture {
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
    vk::Sampler sampler;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};
// the overlay's pictures (avatar, border, icons, state pictures), by texture id
std::unordered_map<ImTextureID, Texture> s_textures;

struct SwapImage {
    vk::ImageView view;
    vk::Framebuffer framebuffer;
};
std::unordered_map<VkImage, SwapImage> s_swap_images;
vk::Extent2D s_swap_extent{};

PFN_vkVoidFunction LoadFunction(const char* name, void* user_data) {
    if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr) {
        return nullptr;
    }
    return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(static_cast<VkInstance>(user_data),
                                                               name);
}

std::optional<u32> FindMemoryType(u32 type_filter, vk::MemoryPropertyFlags properties) {
    const vk::PhysicalDeviceMemoryProperties memory = s_physical_device.getMemoryProperties();
    for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return std::nullopt;
}

void Barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
             vk::AccessFlags src_access, vk::AccessFlags dst_access,
             vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage) {
    const vk::ImageMemoryBarrier barrier{
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmd.pipelineBarrier(src_stage, dst_stage, vk::DependencyFlagBits::eByRegion, {}, {}, barrier);
}

void DestroyTexture(Texture& texture) {
    if (texture.descriptor && s_backend_ready) {
        ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
    }
    if (texture.sampler) {
        s_device.destroySampler(texture.sampler);
    }
    if (texture.view) {
        s_device.destroyImageView(texture.view);
    }
    if (texture.image) {
        s_device.destroyImage(texture.image);
    }
    if (texture.memory) {
        s_device.freeMemory(texture.memory);
    }
    texture = {};
}

// An RGBA8 image as a sampled texture ImGui can draw, uploaded on the spot.
ImTextureID CreateTexture(const unsigned char* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || !s_backend_ready) {
        return 0;
    }
    const vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * 4;
    Texture texture;
    vk::Buffer staging;
    vk::DeviceMemory staging_memory;
    vk::CommandBuffer cmd;
    const auto fail = [&](const char* why) -> ImTextureID {
        LOG_ERROR(Frontend, "tico overlay: no texture ({})", why);
        if (cmd) {
            s_device.freeCommandBuffers(s_command_pool, cmd);
        }
        if (staging) {
            s_device.destroyBuffer(staging);
        }
        if (staging_memory) {
            s_device.freeMemory(staging_memory);
        }
        DestroyTexture(texture);
        return 0;
    };

    staging = s_device.createBuffer({.size = size,
                                     .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                     .sharingMode = vk::SharingMode::eExclusive});
    const vk::MemoryRequirements staging_req = s_device.getBufferMemoryRequirements(staging);
    const auto staging_type =
        FindMemoryType(staging_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                                       vk::MemoryPropertyFlagBits::eHostCoherent);
    if (!staging_type) {
        return fail("no host-visible memory");
    }
    staging_memory = s_device.allocateMemory(
        {.allocationSize = staging_req.size, .memoryTypeIndex = *staging_type});
    s_device.bindBufferMemory(staging, staging_memory, 0);
    std::memcpy(s_device.mapMemory(staging_memory, 0, size), rgba, static_cast<std::size_t>(size));
    s_device.unmapMemory(staging_memory);

    texture.image = s_device.createImage({
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = vk::Extent3D(static_cast<u32>(width), static_cast<u32>(height), 1),
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    const vk::MemoryRequirements image_req = s_device.getImageMemoryRequirements(texture.image);
    const auto image_type =
        FindMemoryType(image_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    if (!image_type) {
        return fail("no device-local memory");
    }
    texture.memory =
        s_device.allocateMemory({.allocationSize = image_req.size, .memoryTypeIndex = *image_type});
    s_device.bindImageMemory(texture.image, texture.memory, 0);
    texture.view = s_device.createImageView({
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    texture.sampler = s_device.createSampler({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    });

    cmd = s_device
              .allocateCommandBuffers({.commandPool = s_command_pool,
                                       .level = vk::CommandBufferLevel::ePrimary,
                                       .commandBufferCount = 1})
              .front();
    cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    Barrier(cmd, texture.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            {}, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer);
    cmd.copyBufferToImage(staging, texture.image, vk::ImageLayout::eTransferDstOptimal,
                          vk::BufferImageCopy{
                              .imageSubresource{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                              .imageExtent = vk::Extent3D(static_cast<u32>(width),
                                                          static_cast<u32>(height), 1),
                          });
    Barrier(cmd, texture.image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader);
    cmd.end();
    // only this upload is waited for, under Dekopon's queue lock for the submit alone
    const vk::Fence uploaded = s_device.createFence({});
    {
        std::scoped_lock lock{*s_queue_mutex};
        s_queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &cmd}, uploaded);
    }
    static_cast<void>(s_device.waitForFences(uploaded, VK_TRUE, UINT64_MAX));
    s_device.destroyFence(uploaded);
    s_device.freeCommandBuffers(s_command_pool, cmd);
    s_device.destroyBuffer(staging);
    s_device.freeMemory(staging_memory);

    texture.descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(texture.sampler),
                                                     static_cast<VkImageView>(texture.view),
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.descriptor));
    s_textures.emplace(id, texture);
    return id;
}

class Host final : public IOverlayHost {
public:
    std::string GetGamePath() override {
        return {};
    }
    bool IsGameLoaded() override {
        return true;
    }
    bool StateSlotExists(int) override {
        return false;
    }
    void SaveStateSlot(int) override {}
    void LoadStateSlot(int) override {}
    void SwapDisc(const std::string&) override {}

    ImTextureID CreateTextureRGBA(const unsigned char* rgba, int width, int height) override {
        return CreateTexture(rgba, width, height);
    }

    void DestroyTexture(ImTextureID id) override {
        const auto it = s_textures.find(id);
        if (it == s_textures.end()) {
            return;
        }
        GameOverlay::DestroyTexture(it->second);
        s_textures.erase(it);
    }
};
Host s_host;

// LOAD and STORE on the swapchain image, in the layout the present path leaves it in (eGeneral
// after the blit, ePresentSrcKHR after Dekopon's own overlay pass), handed back ready to present.
vk::RenderPass CreateRenderPass(vk::Format format, vk::ImageLayout initial_layout) {
    const vk::AttachmentDescription attachment{
        .format = format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = initial_layout,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR,
    };
    const vk::AttachmentReference color{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    const vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask =
            vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead,
    };
    return s_device.createRenderPass({
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    });
}

void DestroySwapImages() {
    for (auto& [image, swap] : s_swap_images) {
        s_device.destroyFramebuffer(swap.framebuffer);
        s_device.destroyImageView(swap.view);
    }
    s_swap_images.clear();
    s_swap_extent = vk::Extent2D{};
}

// A framebuffer for each swapchain image, made the first time it is drawn to.
vk::Framebuffer FramebufferFor(vk::Image image, vk::Extent2D extent) {
    if (extent != s_swap_extent) {
        DestroySwapImages();
        s_swap_extent = extent;
    }
    const auto key = static_cast<VkImage>(image);
    if (const auto it = s_swap_images.find(key); it != s_swap_images.end()) {
        return it->second.framebuffer;
    }
    SwapImage swap;
    swap.view = s_device.createImageView({
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = s_color_format,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    swap.framebuffer = s_device.createFramebuffer({
        .renderPass = s_render_pass,
        .attachmentCount = 1,
        .pAttachments = &swap.view,
        .width = extent.width,
        .height = extent.height,
        .layers = 1,
    });
    s_swap_images.emplace(key, swap);
    return swap.framebuffer;
}

std::atomic_bool s_shader_notice = false;

bool ShaderNoticeEnabled() {
    return s_shader_notice.load(std::memory_order_relaxed);
}

void PublishHudStats() {
    auto& system = Core::System::GetInstance();
    OverlayUI::HudStats stats;
    stats.fps = static_cast<float>(system.GetLastPerfStats().game_fps);
    // from the settings, not the renderer: this runs on the present thread, which the GPU
    // thread waits on, and asking the GPU for its renderer waits for the GPU thread
    if (const u32 scale = Settings::values.resolution_factor.GetValue(); scale != 0) {
        stats.rendered_width = static_cast<int>(400 * scale);
        stats.rendered_height = static_cast<int>(240 * scale);
    }
    // Dekopon's own notice stays off (TicoSettings): tico's HUD draws it
    // kept up a moment after the last build so it can be read
    const auto now = std::chrono::steady_clock::now();
    const u32 pending = VideoCore::GetPendingShaderCompiles();
    if (pending > 0) {
        s_shader_notice_until = now + std::chrono::milliseconds(500);
    }
    if (ShaderNoticeEnabled() && now < s_shader_notice_until) {
        stats.notice = OverlayTranslation::tr("settings_dekopon_compiling_shaders");
        if (pending > 0) {
            stats.notice += "  " + std::to_string(pending);
        }
    }
    OverlayUI::SetHudStats(stats);
}

void Hide() {
    s_visible.store(false);
    s_pending_nav.store(0);
    ImGuiOverlay::SetVisible(false);
    s_shown = false;
}

// Sets up the overlay's Vulkan objects the first time it draws.
bool EnsureDevice(const Vulkan::PresentOverlayTarget& target) {
    if (s_device) {
        return true;
    }
    s_instance = target.instance;
    s_physical_device = target.physical_device;
    s_device = target.device;
    s_queue = target.queue;
    s_queue_family = target.queue_family_index;
    s_image_count = std::max(target.image_count, 2u);
    s_queue_mutex = target.submit_mutex;
    if (!s_instance || !s_device || !s_queue_mutex) {
        s_device = VK_NULL_HANDLE;
        return false;
    }
    s_command_pool = s_device.createCommandPool({
        .flags = vk::CommandPoolCreateFlagBits::eTransient |
                 vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = s_queue_family,
    });
    // ImGui's font atlas and every picture the menu shows
    const vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 128,
    };
    s_descriptor_pool = s_device.createDescriptorPool({
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 128,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    });
    return true;
}

// The time since the last frame the overlay saw, for ImGui.
float FrameDelta() {
    const auto now = std::chrono::steady_clock::now();
    const float delta = std::chrono::duration<float>(now - s_last_frame).count();
    s_last_frame = now;
    return delta > 0.0f && delta < 0.25f ? delta : 1.0f / 60.0f;
}

// Present thread, before the frame is copied to the swapchain: whether tico has anything on
// screen. When it does not, the frame is presented untouched.
bool WantsDraw() {
    if (!s_registered.load(std::memory_order_relaxed)) {
        return false;
    }
    std::scoped_lock draw_lock{s_draw_mutex};
    if (!s_registered.load() || s_failed) {
        return false;
    }
    // the first draw sets the overlay up
    if (!s_ready) {
        return true;
    }

    {
        std::lock_guard lock(s_notice_mutex);
        if (s_pending_notice) {
            s_visible.store(true);
            ImGuiOverlay::SetVisible(true);
            s_shown = true;
            OverlayUI::ShowNotice(std::move(s_pending_notice->first),
                                  std::move(s_pending_notice->second));
            s_pending_notice.reset();
        }
    }
    if (s_cheat_refresh.exchange(false)) {
        OverlayUI::RefreshCheatList();
    }
    if (s_resume_prompt.exchange(false)) {
        s_visible.store(true);
        ImGuiOverlay::SetVisible(true);
        s_shown = true;
        OverlayUI::ShowResumePrompt();
    }

    const bool visible = s_visible.load();
    if (visible != s_shown) {
        ImGuiOverlay::SetVisible(visible);
        s_shown = visible;
    }

    PublishHudStats();
    if (!visible && !OverlayUI::HasTransientContent()) {
        // the next frame drawn starts from now, not from the last one shown
        s_last_frame = std::chrono::steady_clock::now();
        return false;
    }
    return true;
}

// Present thread, once the frame is in `target.image`.
void Draw(const Vulkan::PresentOverlayTarget& target) {
    std::scoped_lock draw_lock{s_draw_mutex};
    const auto pass_through = [&target] {
        // nothing drawn: the image still goes to present
        if (target.layout != vk::ImageLayout::ePresentSrcKHR) {
            Barrier(target.cmdbuf, target.image, target.layout, vk::ImageLayout::ePresentSrcKHR,
                    vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
                    vk::AccessFlagBits::eMemoryRead, vk::PipelineStageFlagBits::eAllCommands,
                    vk::PipelineStageFlagBits::eBottomOfPipe);
        }
    };
    if (!s_registered.load() || s_failed) {
        pass_through();
        return;
    }
    if (!s_ready) {
        s_color_format = target.format;
        if (!EnsureDevice(target) || !ImGuiOverlay::Init(&s_host)) {
            LOG_ERROR(Frontend, "tico overlay could not start; the game runs without a menu");
            s_failed = true;
            pass_through();
            return;
        }
        OverlayUI::ReloadSettings();
        s_last_frame = std::chrono::steady_clock::now();
        s_ready = true;
        PublishHudStats();
    }

    const bool visible = s_visible.load();
    if (visible) {
        const unsigned int nav = s_pending_nav.exchange(0);
        ImGuiOverlay::FeedNav({
            .up = (nav & NavBit_Up) != 0,
            .down = (nav & NavBit_Down) != 0,
            .left = (nav & NavBit_Left) != 0,
            .right = (nav & NavBit_Right) != 0,
            .accept = (nav & NavBit_Accept) != 0,
            .cancel = (nav & NavBit_Cancel) != 0,
        });
        ImGuiOverlay::FeedTouch({s_touch_down.load(), s_touch_x.load(), s_touch_y.load()});
    }

    const vk::Extent2D extent = target.extent;
    ImDrawData* draw_data = ImGuiOverlay::BuildFrame(static_cast<float>(extent.width),
                                                     static_cast<float>(extent.height),
                                                     FrameDelta());
    const Action action = ImGuiOverlay::ConsumeAction();
    if (action != Action::None) {
        s_pending_action.store(static_cast<int>(action));
        if (action == Action::Resume) {
            Hide();
        }
    }
    if (!draw_data) {
        pass_through();
        return;
    }

    const vk::RenderPass pass = target.layout == vk::ImageLayout::ePresentSrcKHR
                                    ? s_render_pass_after_overlays
                                    : s_render_pass;
    target.cmdbuf.beginRenderPass(
        {
            .renderPass = pass,
            .framebuffer = FramebufferFor(target.image, extent),
            .renderArea{.offset = {0, 0}, .extent = extent},
        },
        vk::SubpassContents::eInline);
    {
        // ImGui's backend uploads textures (the font) with a submit and a queue wait of its own
        std::scoped_lock lock{*s_queue_mutex};
        ImGui_ImplVulkan_RenderDrawData(draw_data, static_cast<VkCommandBuffer>(target.cmdbuf));
    }
    target.cmdbuf.endRenderPass();
}

// Called on the present thread with the queue idle, before the swapchain images go.
void Reset() {
    std::scoped_lock draw_lock{s_draw_mutex};
    DestroySwapImages();
}

constexpr Vulkan::PresentOverlayHooks kHooks{
    .wants_draw = &WantsDraw,
    .draw = &Draw,
    .reset = &Reset,
};

} // namespace

void Init() {
    if (s_registered.load()) {
        return;
    }
    s_visible.store(false);
    s_pending_action.store(0);
    s_pending_nav.store(0);
    hidInitializeTouchScreen();

    // last, so no draw runs half set up
    s_registered.store(true);
    Vulkan::SetPresentOverlay(&kHooks);
}

void Shutdown() {
    if (!s_registered.load()) {
        return;
    }
    Vulkan::SetPresentOverlay(nullptr);
    s_registered.store(false);
    // a draw in progress finishes first
    std::scoped_lock draw_lock{s_draw_mutex};
    if (s_device) {
        std::scoped_lock lock{*s_queue_mutex};
        s_device.waitIdle();
    }

    if (s_ready) {
        ImGuiOverlay::Shutdown();
    }
    if (s_device) {
        DestroySwapImages();
        s_device.destroyDescriptorPool(s_descriptor_pool);
        s_device.destroyCommandPool(s_command_pool);
    }
    s_descriptor_pool = VK_NULL_HANDLE;
    s_command_pool = VK_NULL_HANDLE;
    s_device = VK_NULL_HANDLE;
    s_ready = false;
    s_failed = false;
    s_shown = false;
    s_visible.store(false);
    s_pending_action.store(0);
    s_pending_nav.store(0);
}

void Update(PadState* pad) {
    if (!s_registered.load() || !pad) {
        return;
    }

    const u64 held = padGetButtons(pad);
    const bool combo_down = (held & HidNpadButton_Plus) != 0 && (held & HidNpadButton_Minus) != 0;
    if (combo_down && !s_was_combo_down) {
        s_visible.store(!s_visible.load());
        s_pending_nav.store(0);
    }
    s_was_combo_down = combo_down;

    if (!s_visible.load()) {
        s_nav_held_prev = 0;
        s_accept_prev = (held & HidNpadButton_A) != 0;
        s_cancel_prev = (held & HidNpadButton_B) != 0;
        s_touch_down.store(false);
        return;
    }

    // directions fire on press, then repeat while held
    const u64 directions = held & (HidNpadButton_AnyUp | HidNpadButton_AnyDown |
                                   HidNpadButton_AnyLeft | HidNpadButton_AnyRight);
    const auto now = std::chrono::steady_clock::now();
    u64 fire = directions & ~s_nav_held_prev;
    if (fire != 0 && now - s_nav_last_fire < kNavDebounce) {
        fire = 0;
    }
    if (fire != 0) {
        s_nav_next_repeat = now + kNavInitialDelay;
    } else if (directions != 0 && directions == s_nav_held_prev && now >= s_nav_next_repeat) {
        fire = directions;
        s_nav_next_repeat = now + kNavRepeat;
    }
    if (fire != 0) {
        s_nav_last_fire = now;
    }
    s_nav_held_prev = directions;

    const bool accept = (held & HidNpadButton_A) != 0;
    const bool cancel = (held & HidNpadButton_B) != 0;
    unsigned int nav = 0;
    if (fire & HidNpadButton_AnyUp) {
        nav |= NavBit_Up;
    }
    if (fire & HidNpadButton_AnyDown) {
        nav |= NavBit_Down;
    }
    if (fire & HidNpadButton_AnyLeft) {
        nav |= NavBit_Left;
    }
    if (fire & HidNpadButton_AnyRight) {
        nav |= NavBit_Right;
    }
    if (accept && !s_accept_prev) {
        nav |= NavBit_Accept;
    }
    if (cancel && !s_cancel_prev) {
        nav |= NavBit_Cancel;
    }
    s_accept_prev = accept;
    s_cancel_prev = cancel;
    if (nav != 0) {
        s_pending_nav.fetch_or(nav);
    }

    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) && touch.count > 0) {
        s_touch_x.store(static_cast<float>(touch.touches[0].x));
        s_touch_y.store(static_cast<float>(touch.touches[0].y));
        s_touch_down.store(true);
    } else {
        s_touch_down.store(false);
    }
}

bool IsVisible() {
    return s_visible.load();
}

void SetVisible(bool visible) {
    s_visible.store(visible);
    s_pending_nav.store(0);
}

void ShowNotice(std::string message, std::vector<std::string> choices) {
    std::lock_guard lock(s_notice_mutex);
    s_pending_notice.emplace(std::move(message), std::move(choices));
    s_visible.store(true);
}

void ShowResumePrompt() {
    s_resume_prompt.store(true);
    s_visible.store(true);
}

unsigned long long LoadPicture(const std::string& path, float* aspect) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* rgba = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!rgba) {
        return 0;
    }
    const ImTextureID texture = s_host.CreateTextureRGBA(rgba, width, height);
    stbi_image_free(rgba);
    if (aspect && height > 0) {
        *aspect = static_cast<float>(width) / static_cast<float>(height);
    }
    return static_cast<unsigned long long>(texture);
}

void FreePicture(unsigned long long texture) {
    if (texture) {
        s_host.DestroyTexture(static_cast<ImTextureID>(texture));
    }
}

void RequestCheatRefresh() {
    s_cheat_refresh.store(true);
}

void SetShaderNotice(bool enabled) {
    s_shader_notice.store(enabled, std::memory_order_relaxed);
}

Action ConsumeAction() {
    return static_cast<Action>(s_pending_action.exchange(0));
}

} // namespace SwitchFrontend::GameOverlay

// The renderer tico's overlay draws with during a game: ImGui's Vulkan backend,
// recording into Dekopon's present command buffer (see Draw).
namespace SwitchFrontend::GameOverlay {

bool RendererInit() {
    if (s_color_format == vk::Format::eUndefined) {
        return false;
    }
    s_render_pass = CreateRenderPass(s_color_format, vk::ImageLayout::eGeneral);
    s_render_pass_after_overlays = CreateRenderPass(s_color_format, vk::ImageLayout::ePresentSrcKHR);
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, LoadFunction,
                                        static_cast<VkInstance>(s_instance))) {
        LOG_ERROR(Frontend, "tico overlay: ImGui_ImplVulkan_LoadFunctions failed");
        return false;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_1;
    info.Instance = static_cast<VkInstance>(s_instance);
    info.PhysicalDevice = static_cast<VkPhysicalDevice>(s_physical_device);
    info.Device = static_cast<VkDevice>(s_device);
    info.QueueFamily = s_queue_family;
    info.Queue = static_cast<VkQueue>(s_queue);
    info.DescriptorPool = static_cast<VkDescriptorPool>(s_descriptor_pool);
    info.RenderPass = static_cast<VkRenderPass>(s_render_pass);
    info.MinImageCount = s_image_count;
    info.ImageCount = s_image_count;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&info)) {
        LOG_ERROR(Frontend, "tico overlay: ImGui_ImplVulkan_Init failed");
        return false;
    }
    s_backend_ready = true;
    return true;
}

void RendererShutdown() {
    for (auto& [id, texture] : s_textures) {
        DestroyTexture(texture);
    }
    s_textures.clear();
    if (s_backend_ready) {
        ImGui_ImplVulkan_Shutdown();
        s_backend_ready = false;
    }
    if (s_render_pass) {
        s_device.destroyRenderPass(s_render_pass);
        s_render_pass = VK_NULL_HANDLE;
    }
    if (s_render_pass_after_overlays) {
        s_device.destroyRenderPass(s_render_pass_after_overlays);
        s_render_pass_after_overlays = VK_NULL_HANDLE;
    }
    s_color_format = vk::Format::eUndefined;
}

void RendererBeginFrame() {
    ImGui_ImplVulkan_NewFrame();
}

} // namespace SwitchFrontend::GameOverlay
