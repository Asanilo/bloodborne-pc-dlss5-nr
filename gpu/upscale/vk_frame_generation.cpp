// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/vk_frame_generation.h"
#include "upscale/frame_generation_lifecycle.h"
#include "upscale/fg_backend_status.h"
#include "upscale/frame_generation_contract.h"
#include "bbport_toggles.h"

#include <atomic>
#include <chrono>
#include <cstdio>

#include <vk_mem_alloc.h>

#include "bbport_settings.h"
#include "buffers/buffer.h"
#include "ffx_vk_portable.h"
#include "ffx_vk_fsr3_3_1_5_bridge.h"
#include <cstring>
#include "render/scheduler.h"
#include "vulkan/instance.h"
#include "upscale/dlssg_input_capture.h"
#include "upscale/dlssg_cpu_bridge.h"
#include "upscale/dlssg_gpu_bridge.h"

namespace Vulkan {

namespace {

constexpr vk::ImageSubresourceRange ColorRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

FfxVkPortableImage Describe(vk::Image image, vk::Format format, u32 width, u32 height,
                            vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect,
                            FfxVkPortableResourceState state) {
    FfxVkPortableImage out{};
    out.structSize = sizeof(out);
    out.image = image;
    out.format = static_cast<VkFormat>(format);
    out.extent = {width, height};
    out.mipCount = 1;
    out.arrayLayers = 1;
    out.usage = static_cast<VkImageUsageFlags>(usage);
    out.aspect = static_cast<VkImageAspectFlags>(aspect);
    out.state = state;
    return out;
}

/// The output-size images the frame generation reads and writes: the scene's image (the UI
/// image of the upscaler), the HUD-less copy and the outputs.
constexpr vk::ImageUsageFlags ColorUsage = vk::ImageUsageFlagBits::eStorage |
                                           vk::ImageUsageFlagBits::eSampled |
                                           vk::ImageUsageFlagBits::eTransferSrc |
                                           vk::ImageUsageFlagBits::eTransferDst;

void ImageBarrier(vk::CommandBuffer cmdbuf, vk::Image image, vk::PipelineStageFlags2 src_stage,
                  vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage,
                  vk::AccessFlags2 dst_access, vk::ImageLayout old_layout = vk::ImageLayout::eGeneral) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = ColorRange,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

FfxVkFsr3_3_1_6FrameGenerationImage Describe316(const FfxVkPortableImage& image) {
    return {image.image, image.format, image.extent.width, image.extent.height,
            VK_IMAGE_LAYOUT_GENERAL, image.usage};
}

int Record316(FfxVkFsr3_3_1_6FrameGenerationContext* context,
              const FfxVkPortableFrameGenerationPrepareInfo& prepare,
              const FfxVkPortableFrameGenerationDispatchInfo& dispatch) {
    FfxVkFsr3_3_1_6FrameGenerationPrepareInfo p{};
    p.commandBuffer = prepare.commandBuffer;
    p.color = Describe316(dispatch.currentColor);
    p.depth = Describe316(prepare.depth);
    p.motionVectors = Describe316(prepare.motionVectors);
    p.renderWidth = prepare.renderSize.width; p.renderHeight = prepare.renderSize.height;
    p.jitterOffsetX = prepare.jitterOffset.x; p.jitterOffsetY = prepare.jitterOffset.y;
    p.motionVectorScaleX = prepare.motionVectorScale.x;
    p.motionVectorScaleY = prepare.motionVectorScale.y;
    p.frameTimeMilliseconds = prepare.frameTimeMilliseconds;
    p.minLuminance = prepare.minLuminance; p.maxLuminance = prepare.maxLuminance;
    p.transferFunction = FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SRGB;
    p.cameraNear = prepare.cameraNear; p.cameraFar = prepare.cameraFar;
    p.viewSpaceToMeters = prepare.viewSpaceToMeters;
    p.cameraVerticalFovRadians = prepare.cameraVerticalFovRadians;
    const auto camera = [](float (&out)[3], const auto& in) {
        out[0] = in.x; out[1] = in.y; out[2] = in.z;
    };
    camera(p.cameraPosition, prepare.cameraPosition); camera(p.cameraUp, prepare.cameraUp);
    camera(p.cameraRight, prepare.cameraRight); camera(p.cameraForward, prepare.cameraForward);
    p.frameId = prepare.frameId; p.reset = prepare.reset;
    auto result = ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(context, &p);
    if (result != FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) return int(result);
    FfxVkFsr3_3_1_6FrameGenerationDispatchInfo d{};
    d.commandBuffer = dispatch.commandBuffer;
    d.color = p.color; d.output = Describe316(dispatch.output);
    d.displayWidth = dispatch.displaySize.width; d.displayHeight = dispatch.displaySize.height;
    d.frameTimeMilliseconds = p.frameTimeMilliseconds;
    d.cameraNear = p.cameraNear; d.cameraFar = p.cameraFar;
    d.viewSpaceToMeters = p.viewSpaceToMeters; d.cameraVerticalFovRadians = p.cameraVerticalFovRadians;
    d.minLuminance = p.minLuminance; d.maxLuminance = p.maxLuminance;
    d.transferFunction = p.transferFunction; d.frameId = p.frameId; d.reset = p.reset;
    return int(ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &d));
}

} // namespace

namespace {
std::atomic<u64> stat_captured{0}, stat_interpolated{0}, stat_ready{0}, stat_taken{0}, stat_missed{0};
void Stats() {
    static std::atomic<u64> calls{0};
    if (++calls % 2000 == 0) {
        std::printf("Frame generation stats: captured %llu interpolated %llu ready %llu taken %llu "
                    "missed %llu\n", (unsigned long long)stat_captured.load(),
                    (unsigned long long)stat_interpolated.load(), (unsigned long long)stat_ready.load(),
                    (unsigned long long)stat_taken.load(), (unsigned long long)stat_missed.load());
    }
}
} // namespace

FrameGeneration::FrameGeneration(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {
    FfxVkPortableDeviceCapabilities caps{};
    caps.structSize = sizeof(caps);
    auto& settings = BbSettings::Get();
    if (ffxVkPortableQueryDeviceCapabilities(instance.GetPhysicalDevice(), &caps) !=
        FFX_VK_PORTABLE_OK) {
        settings.frame_generation_problem = "the GPU's capabilities could not be read";
    } else if (!caps.fsr3FrameGenerationPrerequisites) {
        settings.frame_generation_problem = "the GPU lacks what FSR 3 frame generation needs";
    } else {
        supported = true;
    }
    const auto* selected=FgBackendStatus::Selected();
    if (!FgBackendStatus::Application()) supported=false;
    if (FgBackendStatus::Is(selected,"dlssg") && instance.GetVendorID()!=0x10de) {
        supported=false;settings.frame_generation_problem="DLSS FG requires a supported NVIDIA GPU";
    }
    settings.frame_generation_supported = supported;
    const char* backend = std::getenv("BB_FRAME_GEN_BACKEND");
    use316 = backend && std::strcmp(backend, "fsr316") == 0;
    if (backend && std::strcmp(backend, "dlssg-capture") == 0)
        dlssg_capture = std::make_unique<DlssgInputCapture>(instance, scheduler);
    if (backend && std::strcmp(backend, "dlssg") == 0) {
        const char* transport = std::getenv("BB_DLSSG_TRANSPORT");
        if (transport && std::strcmp(transport, "gpu-dmabuf-v1") == 0) {
            dlssg_gpu = std::make_unique<DlssgGpuBridge>(instance, scheduler);
        } else {
            dlssg_capture = std::make_unique<DlssgInputCapture>(instance, scheduler, true);
            dlssg_bridge = std::make_unique<DlssgCpuBridge>();
        }
    }
    const char* sm = std::getenv("NVPRESENT_ENABLE_SMOOTH_MOTION");
    const bool smooth_motion = sm && std::strcmp(sm, "1") == 0;
    FgLifecycle::driver_smooth_motion.store(smooth_motion);
    FgLifecycle::enabled.store(FgBackendStatus::Application() || smooth_motion);
    std::printf("FG_BACKEND selected=%s application_enabled=%u driver_requested=%u supported=%u\n",
                selected,unsigned(settings.frame_generation.load()),unsigned(smooth_motion),unsigned(supported));
}

FrameGeneration::~FrameGeneration() {
    Destroy();
}

bool FrameGeneration::Wanted() const {
    return supported && !failed && !FgLifecycle::driver_smooth_motion.load() &&
           !FgLifecycle::exit_requested.load() &&
           BbSettings::Get().frame_generation.load(std::memory_order_relaxed);
}

void FrameGeneration::Retire() {
    if (!FgLifecycle::Pending()) return;
    async_output.reset();
    ResolveDeferredGpuOutput();
    // Includes every encode from our output into a per-Frame image, not only FG dispatches.
    scheduler.Finish();
    if (dlssg_bridge && !dlssg_bridge->Retire()) return;
    if (dlssg_gpu && !dlssg_gpu->Retire()) return;
    Destroy();
    std::scoped_lock lock{targets_mutex};
    targets.clear();
    depth_copy = VideoCore::HostImage{};
    depth_transfer.reset();
    hudless = VideoCore::HostImage{};
    scene_captured = hudless_ready = false;
    FgLifecycle::draw_retired.store(true);
    std::printf("FG_DRAW_RETIRED actual_GPU_complete=1 context_targets_depth_hudless_released=1\n");
}

void FrameGeneration::OnFrameStart() {
    // Consume a one-shot request at a real-frame boundary, like the experimental menu switch.
    // Writers should rename a complete file containing 0 or 1 onto this private path.
    static const char* control = std::getenv("BB_FRAME_GEN_CONTROL");
    if (control) {
        if (FILE* file = std::fopen(control, "r")) {
            int value = -1;
            const bool valid = std::fscanf(file, "%d", &value) == 1 && (value == 0 || value == 1);
            std::fclose(file);
            if (valid && std::remove(control) == 0)
                BbSettings::Get().frame_generation.store(value == 1, std::memory_order_relaxed);
        }
    }
    const u64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!Wanted() || !dispatched_last_frame || now - last_frame_ns > 250'000'000) {
        reset_next = true;
    }
    last_frame_ns = now;
    dispatched_last_frame = false;
    scene_captured = hudless_ready = false;
    // Rasterizer calls this immediately AFTER Interpolate, before the display draw and
    // PrepareFrame. Its one-shot output must survive until that flip consumes it.
}

void FrameGeneration::Destroy() {
    async_output.reset();
    if (dlssg_gpu) {
        ResolveDeferredGpuOutput();
        scheduler.Finish();
        if (!dlssg_gpu->Retire()) {
            // Recording closures capture the owner; unknown GPU completion retains it too.
            (void)dlssg_gpu.release();
            return;
        }
        dlssg_gpu.reset();
    }
    if (dlssg_bridge) {
        scheduler.Finish();
        if (!dlssg_bridge->Retire()) return; // No normal draw-retired proof on unknown Helper state.
        dlssg_bridge.reset();
        dlssg_upload.reset();
    }
    if (dlssg_capture) {
        scheduler.Finish();
        dlssg_capture.reset();
    }
    if (context316) {
        scheduler.Finish(); // Runs actual-completion RetireFrame callbacks before destruction.
        std::scoped_lock lock{context316_mutex};
        ffxVkFsr3_3_1_6FrameGenerationContextDestroy(context316);
        context316 = nullptr;
    }
    if (context) {
        scheduler.Finish();
        ffxVkPortableFrameGenerationContextDestroy(context);
        context = nullptr;
    }
}

bool FrameGeneration::EnsureContext(u32 render_width, u32 render_height, u32 width, u32 height,
                                    vk::Format format) {
    if ((context || context316) && context_render_w == render_width && context_render_h == render_height &&
        context_w == width && context_h == height && context_format == format) {
        return true;
    }
    Destroy();
    // Destroy() has waited for the draw stream, including the presenter's source encode.
    // The HUD-less copy belongs to the context's size/format, not the session's first size.
    hudless = VideoCore::HostImage{};
    hudless_ready = false;
    const auto device = instance.GetDevice();
    FfxVkPortableDeviceInfo device_info{};
    device_info.structSize = sizeof(device_info);
    device_info.instance = instance.GetInstance();
    device_info.physicalDevice = instance.GetPhysicalDevice();
    device_info.device = device;
    device_info.getDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    device_info.queue = instance.GetGraphicsQueue();
    device_info.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex();
    device_info.shaderFloat16Enabled = instance.IsShaderFloat16Enabled();
    device_info.subgroupSizeControlEnabled = instance.IsSubgroupSizeControlEnabled();
    device_info.synchronization2Enabled = VK_TRUE;
    device_info.shaderStorageImageWriteWithoutFormatEnabled = VK_TRUE;

    FfxVkPortableFrameGenerationCreateInfo create{};
    create.structSize = sizeof(create);
    create.maxRenderSize = {render_width, render_height};
    create.displaySize = {width, height};
    create.interpolationSourceFormat = static_cast<VkFormat>(format);
    create.outputFormat = static_cast<VkFormat>(format);
    if (const u64 issues = ffxVkPortableValidateFrameGenerationCreateInfo(&create)) {
        std::printf("Frame generation: invalid context (%s)\n", ffxVkPortableValidationIssueName(issues));
        failed = true;
        return false;
    }
    bool created = false;
    if (use316) {
        const FfxVkFsr3_3_1_6FrameGenerationCreateInfo info{
            instance.GetPhysicalDevice(), device, render_width, render_height,
            width, height, static_cast<VkFormat>(format),
        };
        created = ffxVkFsr3_3_1_6FrameGenerationContextCreate(&info, &context316) ==
                  FFX_VK_FSR3_3_1_6_FRAMEGEN_OK;
    } else {
        created = ffxVkPortableFrameGenerationContextCreate(&device_info, &create, &context) ==
                  FFX_VK_PORTABLE_OK;
    }
    if (!created) {
        std::printf("Frame generation: the context could not be made\n");
        context = nullptr;
        failed = true;
        return false;
    }
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    reset_next = true;
    std::printf("Frame generation backend: %s\n", use316 ? "FSR 3.1.6 (SDK 2.3) experimental" : "FSR 3.1 (SDK 1.1.4)");
    std::printf("Frame generation: %ux%u from %ux%u scenes (%s)\n", width, height, render_width,
                render_height, vk::to_string(format).c_str());
    return true;
}

void FrameGeneration::CaptureScene(const Scene& in) {
    if (async_output) { async_output.reset(); reset_next = true; }
    ResolveDeferredGpuOutput(); // Before scene fields, guide queries or mailbox inputs can change.
    scene_capture_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    scene_captured = false;
    if (dlssg_capture || dlssg_gpu) {
        scene = in;
        scene_captured = Wanted() && (dlssg_gpu ? dlssg_gpu->Guides(in) : dlssg_capture->Guides(in));
        scene_id = in.source_game_frame;
        scene_reset = reset_next || in.reset;
        if (scene_captured) reset_next = false;
        return;
    }
    if (!Wanted() || !in.depth || !in.motion || !in.width || !in.height ||
        !in.render_width || !in.render_height ||
        !EnsureContext(in.render_width, in.render_height, in.width, in.height,
                                    in.color_format)) {
        reset_next = true;
        return;
    }
    scene = in;
    scene_captured = true;
    scene_id = ++frame_id;
    hudless_ready = false;
    scene_reset = reset_next || in.reset;
    reset_next = false;
    ++stat_captured;
    // The frame is interpolated as shown, UI and all (no HUD-less image: what the game draws over
    // the upscaled scene is not only its HUD, and FSR pastes any difference uninterpolated,
    // a double image). Optical flow needs that finished frame: Prepare runs at the display pass.
    // The depth is copied now, while it is this frame's (the game reuses its depth buffer).
    const auto copy_format = use316 ? vk::Format::eR32Sfloat : in.depth_format;
    if (use316 && in.depth_format != vk::Format::eD32Sfloat &&
        in.depth_format != vk::Format::eD32SfloatS8Uint) {
        scene_captured = false;
        reset_next = true;
        std::printf("Frame generation: 3.1.6 needs a 32-bit float depth source\n");
        return;
    }
    if (!depth_copy || depth_copy_format != copy_format || depth_copy_w != in.render_width ||
        depth_copy_h != in.render_height) {
        scheduler.Finish();
        depth_copy = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        depth_copy.Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = copy_format,
            .extent = {in.render_width, in.render_height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                     (use316 ? vk::ImageUsageFlags{} : vk::ImageUsageFlagBits::eDepthStencilAttachment),
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        if (use316) depth_transfer = std::make_unique<VideoCore::Buffer>(instance, 0,
            u64(in.render_width) * in.render_height * sizeof(float), VideoCore::MemoryType::DeviceLocal,
            "FG 3.1.6 depth format transfer");
        depth_copy_format = copy_format;
        depth_copy_w = in.render_width;
        depth_copy_h = in.render_height;
    }
    scheduler.Record([depth = in.depth, copy = vk::Image(depth_copy), w = in.render_width,
                      h = in.render_height, buffer = depth_transfer ? depth_transfer->Handle() : vk::Buffer{}]
                     (vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
        const vk::ImageSubresourceRange copy_range = buffer ? ColorRange : range;
        const std::array<vk::ImageMemoryBarrier2, 2> before{{
            {.srcStageMask = all, .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
             .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .image = depth, .subresourceRange = range},
            {.srcStageMask = all, .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
             .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
             .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
             .oldLayout = vk::ImageLayout::eUndefined, .newLayout = vk::ImageLayout::eGeneral,
             .image = copy, .subresourceRange = copy_range},
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = before.data()});
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
            .extent = {w, h, 1},
        };
        if (buffer) {
            // Image->buffer extracts exactly the D32 float aspect; buffer->image writes the
            // same bytes as R32F. No incompatible depth/color image copy and no CPU readback.
            const vk::BufferMemoryBarrier2 writable{
                .srcStageMask = all,
                .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
                .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .buffer = buffer, .size = VK_WHOLE_SIZE,
            };
            cmdbuf.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &writable});
            vk::BufferImageCopy transfer{
                .imageSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1}, .imageExtent = {w, h, 1},
            };
            cmdbuf.copyImageToBuffer(depth, vk::ImageLayout::eGeneral, buffer, transfer);
            auto readable = writable;
            readable.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            readable.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            readable.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            cmdbuf.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &readable});
            transfer.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            cmdbuf.copyBufferToImage(buffer, copy, vk::ImageLayout::eGeneral, transfer);
        } else {
            cmdbuf.copyImage(depth, vk::ImageLayout::eGeneral, copy, vk::ImageLayout::eGeneral, region);
        }
        const std::array<vk::ImageMemoryBarrier2, 2> after{{
            {.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
             .srcAccessMask = vk::AccessFlagBits2::eTransferRead, .dstStageMask = all,
             .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .image = depth, .subresourceRange = range},
            {.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
             .srcAccessMask = vk::AccessFlagBits2::eTransferWrite, .dstStageMask = all,
             .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .image = copy, .subresourceRange = copy_range},
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = after.data()});
    });
}


bool FrameGeneration::BindSceneColor(vk::Image color, vk::Format format, u32 width, u32 height,
                                     u64 source) {
    if (!Wanted() || !scene_captured || !color || scene.color || scene.source_game_frame != source ||
        scene.color_format != format || scene.width != width || scene.height != height) return false;
    scene.color = color;
    std::printf("FG_NATIVE_BIND source_game_frame=%llu fg_frame=%llu output=%ux%u hdr_SR=1\n",
        (unsigned long long)source, (unsigned long long)scene_id, width, height);
    return true;
}

void FrameGeneration::CaptureHudless(vk::Image color) {
    if (dlssg_capture || dlssg_gpu) return;
    static const bool enabled = [] {
        const char* value = std::getenv("BB_FRAME_GEN_HUDLESS");
        return value && value[0] == '1';
    }();
    if (!enabled) return;
    if (!color || !scene.color || !scene_captured || color != scene.color || hudless_ready) {
        return;
    }
    if (!hudless) {
        scheduler.Finish();
        hudless = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        hudless.Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = context_format,
            .extent = {context_w, context_h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = ColorUsage,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
    }
    hudless_ready = true;
    scheduler.EndRendering();
    scheduler.Record([color, copy = vk::Image(hudless), w = scene.width,
                      h = scene.height](vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                     vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        ImageBarrier(cmdbuf, copy, all, vk::AccessFlagBits2::eMemoryRead,
                     vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                     vk::ImageLayout::eUndefined);
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {w, h, 1},
        };
        cmdbuf.copyImage(color, vk::ImageLayout::eGeneral, copy, vk::ImageLayout::eGeneral, region);
        ImageBarrier(cmdbuf, color, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferRead, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        ImageBarrier(cmdbuf, copy, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead);
    });
}

FrameGeneration::Target& FrameGeneration::PrepareTarget(VAddr display, u64 id, bool reset) {
    Target* target;
    {
        std::scoped_lock lock{targets_mutex};
        // One output for all display buffers: the interpolation, the display pass and the
        // presenter's encode are all in the draw stream's order. Bind the destination even
        // though storage is shared: another VideoOut address cannot consume this output.
        target = &targets[0];
        if (!target->image || target->format != scene.color_format || target->width != scene.width ||
            target->height != scene.height) {
            scheduler.Finish(); // its last encode may still read it
            const auto device = instance.GetDevice();
            target->view.reset();
            target->image = VideoCore::HostImage(device, instance.GetAllocator());
            target->image.Create(vk::ImageCreateInfo{
                // The presenter may view it as sRGB (as it views the display buffer).
                .flags = vk::ImageCreateFlagBits::eMutableFormat |
                         vk::ImageCreateFlagBits::eExtendedUsage,
                .imageType = vk::ImageType::e2D,
                .format = scene.color_format,
                .extent = {scene.width, scene.height, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = ColorUsage,
                .initialLayout = vk::ImageLayout::eUndefined,
            });
            target->view = Check<"create a frame generation view">(device.createImageViewUnique({
                .image = vk::Image(target->image),
                .viewType = vk::ImageViewType::e2D,
                .format = scene.color_format,
                .subresourceRange = ColorRange,
            }));
            target->format = scene.color_format;
            target->width = scene.width;
            target->height = scene.height;
        }
        // Ready as recorded (the presenter's encode comes later in the same stream). The first
        // frame after a reset has nothing before it to interpolate from.
        target->ready = !reset;
        target->frame = id;
        target->source_game_frame = scene.source_game_frame;
        target->display = display;
        target->eligible.reset();
        target->capture_cpu_ns = scene_capture_ns;
        target->recorded = std::make_shared<std::atomic<bool>>(false);
        if (target->ready) ++stat_ready;
    }

    return *target;
}

void FrameGeneration::Interpolate(vk::Image color, VAddr display) {
    if (dlssg_gpu) {
        ResolveDeferredGpuOutput(); // A prior display pass that never flipped is discarded.
        if (Wanted() && scene_captured && color && color == scene.color) {
            const u64 previous_source = dlssg_gpu->LastSource();
            const bool eligible = dlssg_gpu->Evaluate(scene, color, display, scene_reset);
            dispatched_last_frame = true;
            if (dlssg_gpu->Independent()) {
                async_output = eligible ? dlssg_gpu->TakeTicket() : nullptr;
                if (async_output) async_output->capture_cpu_ns = scene_capture_ns;
            } else if (eligible && !FgLifecycle::held.load()) {
                auto& target = PrepareTarget(display, scene_id, false);
                target.strict_source = true;
                target.previous_source_game_frame = previous_source;
                target.eligible = dlssg_gpu->Eligibility();
                if (dlssg_gpu->Late()) {
                    deferred_gpu_image = vk::Image(target.image);
                    deferred_gpu_display = display;
                    deferred_gpu_recorded = target.recorded;
                } else dlssg_gpu->Consume(vk::Image(target.image), target.recorded);
            } else {
                dlssg_gpu->Consume({}); // Disabled still returns output ownership after real GPU use.
            }
        }
        scene_captured = false;
        return;
    }
    if (dlssg_capture) {
        if (Wanted() && scene_captured && color && color == scene.color) {
            if (!dlssg_bridge) dlssg_capture->Color(scene, color);
            else if (dlssg_capture->ReadColor(scene, color)) {
                if (!dlssg_upload) dlssg_upload = std::make_unique<VideoCore::Buffer>(instance, 0,
                    u64(scene.width)*scene.height*4, VideoCore::MemoryType::HostUncached, "DLSSG Native owned upload");
                const u64 previous_source = dlssg_bridge->LastSource();
                const bool eligible = dlssg_bridge->Exchange(scene, display, scene_reset, *dlssg_capture, *dlssg_upload);
                dispatched_last_frame = true;
                if (eligible && !FgLifecycle::held.load()) {
                    auto& target = PrepareTarget(display, scene_id, false);
                    target.strict_source = true;
                    target.previous_source_game_frame = previous_source;
                    scheduler.Record([image=vk::Image(target.image),buffer=dlssg_upload->Handle(),
                                      recorded=target.recorded,w=scene.width,h=scene.height,
                                      timing=&dlssg_capture->timing,source=scene.source_game_frame](vk::CommandBuffer cmd) {
                        timing->upload_source=source;timing->upload_pending=bool(timing->pool);timing->Mark(cmd,4);
                        const vk::BufferMemoryBarrier2 upload{
                            .srcStageMask=vk::PipelineStageFlagBits2::eHost,.srcAccessMask=vk::AccessFlagBits2::eHostWrite,
                            .dstStageMask=vk::PipelineStageFlagBits2::eCopy,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,
                            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                            .buffer=buffer,.size=VK_WHOLE_SIZE,
                        };
                        cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&upload});
                        ImageBarrier(cmd,image,vk::PipelineStageFlagBits2::eAllCommands,vk::AccessFlagBits2::eMemoryRead,
                            vk::PipelineStageFlagBits2::eCopy,vk::AccessFlagBits2::eTransferWrite,vk::ImageLayout::eUndefined);
                        const vk::BufferImageCopy copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={w,h,1}};
                        cmd.copyBufferToImage(buffer,image,vk::ImageLayout::eGeneral,copy);
                        ImageBarrier(cmd,image,vk::PipelineStageFlagBits2::eCopy,vk::AccessFlagBits2::eTransferWrite,
                            vk::PipelineStageFlagBits2::eFragmentShader,vk::AccessFlagBits2::eShaderRead);
                        timing->Mark(cmd,5);recorded->store(true,std::memory_order_release);
                    });
                }
            }
        }
        scene_captured = false;
        return;
    }
    Stats();
    if (!FgContract::CanInterpolate(Wanted(), scene_captured, color, scene.color,
                                    context || context316)) {
        std::scoped_lock lock{targets_mutex};
        for (auto& [address, target] : targets) target.ready = false;
        reset_next = true;
        return;
    }
    ++stat_interpolated;
    scene_captured = false;
    dispatched_last_frame = true;
    const u64 id = scene_id;
    // OnFrameStart is after this call at the display-copy boundary. A pause must invalidate
    // the FIRST resumed dispatch, not the one after it. No generated output on that reset.
    const u64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool reset = scene_reset || !last_dispatch_ns || now - last_dispatch_ns > 250'000'000;
    last_dispatch_ns = now;
    if (reset) std::printf("FG_HISTORY_RESET fg_frame=%llu scene_reset=%u gap_reset=%u\n",
        (unsigned long long)id, scene_reset,
        !scene_reset && reset);

    Target* target = &PrepareTarget(display, id, reset);

    FfxVkPortableFrameGenerationDispatchInfo dispatch{};
    dispatch.structSize = sizeof(dispatch);
    dispatch.currentColor = Describe(color, scene.color_format, scene.width, scene.height,
                                     ColorUsage | vk::ImageUsageFlagBits::eColorAttachment,
                                     vk::ImageAspectFlagBits::eColor,
                                     FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    // The UI-less frame (CaptureHudless): FSR interpolates it and lays the UI over unmoved. Without
    // one (no UI drawn this frame) the whole frame is interpolated.
    static const bool use_hudless = [] {
        const char* value = std::getenv("BB_FRAME_GEN_HUDLESS");
        return value && value[0] == '1';
    }();
    if (use_hudless && hudless_ready && !BbToggle::Disabled(1u << 30)) {
        dispatch.hudlessColor = Describe(vk::Image(hudless), scene.color_format, scene.width,
                                         scene.height, ColorUsage, vk::ImageAspectFlagBits::eColor,
                                         FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    } else {
        dispatch.hudlessColor.structSize = sizeof(dispatch.hudlessColor); // none: described, null
    }
    hudless_ready = false;
    dispatch.distortionField.structSize = sizeof(dispatch.distortionField); // absent: described, null
    dispatch.output = Describe(vk::Image(target->image), scene.color_format, scene.width,
                               scene.height, ColorUsage, vk::ImageAspectFlagBits::eColor,
                               FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.displaySize = {scene.width, scene.height};
    dispatch.interpolationRect = {0, 0, scene.width, scene.height};
    dispatch.frameTimeMilliseconds = scene.frame_ms;
    dispatch.cameraNear = scene.camera_near;
    dispatch.cameraFar = scene.camera_far;
    dispatch.cameraVerticalFovRadians = scene.vertical_fov;
    dispatch.viewSpaceToMeters = 1.0f;
    dispatch.minLuminance = 0.0f;
    dispatch.maxLuminance = 1.0f;
    dispatch.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    dispatch.reset = reset ? VK_TRUE : VK_FALSE;
    dispatch.frameId = id;

    {
        // Checked once with a placeholder command buffer (the real one is the recording thread's).
        static bool checked = false;
        if (!checked) {
            checked = true;
            FfxVkPortableFrameGenerationCreateInfo create{};
            create.structSize = sizeof(create);
            create.maxRenderSize = {context_render_w, context_render_h};
            create.displaySize = {context_w, context_h};
            create.interpolationSourceFormat = static_cast<VkFormat>(context_format);
            create.outputFormat = static_cast<VkFormat>(context_format);
            auto d = dispatch;
            d.commandBuffer = reinterpret_cast<VkCommandBuffer>(uintptr_t{1});
            if (const u64 issues = ffxVkPortableValidateFrameGenerationDispatchInfo(&create, &d)) {
                std::printf("Frame generation: dispatch: %s\n", ffxVkPortableValidationIssueName(issues));
            }
        }
    }
    FfxVkPortableFrameGenerationPrepareInfo prepare{};
    prepare.structSize = sizeof(prepare);
    prepare.depth = Describe(vk::Image(depth_copy), depth_copy_format, scene.render_width,
                             scene.render_height, vk::ImageUsageFlagBits::eSampled,
                             use316 ? vk::ImageAspectFlagBits::eColor : vk::ImageAspectFlagBits::eDepth,
                             FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.motionVectors = Describe(scene.motion, vk::Format::eR16G16Sfloat, scene.render_width,
                                     scene.render_height,
                                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                                     vk::ImageAspectFlagBits::eColor,
                                     FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.renderSize = {scene.render_width, scene.render_height};
    prepare.jitterOffset = {scene.jitter_x, scene.jitter_y};
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeMilliseconds = scene.frame_ms;
    prepare.cameraNear = scene.camera_near;
    prepare.cameraFar = scene.camera_far;
    prepare.cameraVerticalFovRadians = scene.vertical_fov;
    prepare.viewSpaceToMeters = 1.0f;
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = 1.0f;
    prepare.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    prepare.cameraPosition = {scene.camera_position[0], scene.camera_position[1], scene.camera_position[2]};
    prepare.cameraUp = {scene.camera_up[0], scene.camera_up[1], scene.camera_up[2]};
    prepare.cameraRight = {scene.camera_right[0], scene.camera_right[1], scene.camera_right[2]};
    prepare.cameraForward = {scene.camera_forward[0], scene.camera_forward[1], scene.camera_forward[2]};
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    prepare.frameId = id;
    const FfxVkPortableImage source =
        dispatch.hudlessColor.image != VK_NULL_HANDLE ? dispatch.hudlessColor : dispatch.currentColor;

    scheduler.EndRendering();
    scheduler.Record([this, prepare, source, dispatch, color, out = vk::Image(target->image),
                      recorded = target->recorded, width = target->width,
                      height = target->height](vk::CommandBuffer cmdbuf) mutable {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        // The UI drew into it; the display pass reads it after.
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                     vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
        ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryRead,
                     vk::PipelineStageFlagBits2::eComputeShader,
                     vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eUndefined);
        prepare.commandBuffer = cmdbuf;
        dispatch.commandBuffer = cmdbuf;
        int prepared = 0, dispatched = 0;
        if (use316) {
            std::scoped_lock lock{context316_mutex};
            dispatched = Record316(context316, prepare, dispatch);
        } else {
            prepared = int(ffxVkPortableFrameGenerationContextPrepare(context, &prepare, &source));
            dispatched = prepared == 0
                ? int(ffxVkPortableFrameGenerationContextRecordDispatch(context, &dispatch)) : prepared;
        }
        const bool ok = dispatched == 0;
        recorded->store(ok, std::memory_order_release);
        if (!ok) {
            static int told = 0;
            if (told++ < 4) {
                std::printf("Frame generation: the interpolation could not be recorded (%s %d)\n",
                            prepared != 0 ? "prepare" : "dispatch", dispatched);
            }
        }
        ImageBarrier(cmdbuf, color, vk::PipelineStageFlagBits2::eComputeShader,
                     vk::AccessFlagBits2::eShaderRead, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        ImageBarrier(cmdbuf, out, vk::PipelineStageFlagBits2::eComputeShader,
                     vk::AccessFlagBits2::eShaderStorageWrite, all, vk::AccessFlagBits2::eMemoryRead);
        if (!ok) {
            // Shown anyway (it was handed out as ready): the real frame twice, not garbage.
            ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryRead,
                         vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                         vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
            cmdbuf.copyImage(color, vk::ImageLayout::eGeneral, out, vk::ImageLayout::eGeneral,
                             vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                           .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                           .extent = {width, height, 1}});
            ImageBarrier(cmdbuf, out, vk::PipelineStageFlagBits2::eTransfer,
                         vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead);
            ImageBarrier(cmdbuf, color, vk::PipelineStageFlagBits2::eTransfer,
                         vk::AccessFlagBits2::eTransferRead, all,
                         vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        }
    });
    if (use316) {
        // This callback is invoked only after the draw submission's timeline tick completed.
        // It covers partial failed records as well; recording success is never retirement.
        scheduler.DeferPriorityOperation([this, ctx = context316, id] {
            std::scoped_lock lock{context316_mutex};
            ffxVkFsr3_3_1_6FrameGenerationContextRetireFrame(ctx, id);
        });
    }
}

void FrameGeneration::ResolveDeferredGpuOutput(VAddr display) {
    if (!deferred_gpu_image) return;
    const bool copy = Wanted() && display && display == deferred_gpu_display;
    dlssg_gpu->Consume(copy ? deferred_gpu_image : vk::Image{},
                       copy ? deferred_gpu_recorded : nullptr);
    deferred_gpu_image = vk::Image{};
    deferred_gpu_recorded.reset();
    deferred_gpu_display = 0;
    if (!copy) {
        std::scoped_lock lock{targets_mutex};
        if (const auto it = targets.find(0); it != targets.end()) it->second.ready = false;
        reset_next = true;
    }
}

std::shared_ptr<DlssgOutputTicket> FrameGeneration::TakeAsyncOutput(VAddr display) {
    auto ticket = std::exchange(async_output, {});
    if (!ticket) return {};
    if (!Wanted() || ticket->display != display) { reset_next = true; return {}; }
    return ticket;
}

bool FrameGeneration::TakeOutput(VAddr display, Output& out) {
    ResolveDeferredGpuOutput(display); // Same recording owner, after the guest EOP wait.
    if (!Wanted()) {
        return false;
    }
    std::scoped_lock lock{targets_mutex};
    const auto it = targets.find(0);
    if (it == targets.end() || !it->second.ready) {
        ++stat_missed;
        return false;
    }
    Target& t = it->second;
    t.ready = false; // including a rejected destination: never leave stale output for later
    if (t.display != display) {
        ++stat_missed;
        reset_next = true;
        return false;
    }
    ++stat_taken;
    out = {vk::Image(t.image), *t.view, t.format, t.width, t.height, t.frame, t.recorded,
           t.source_game_frame, t.strict_source, t.previous_source_game_frame, t.eligible,
           t.capture_cpu_ns};
    return true;
}

} // namespace Vulkan
