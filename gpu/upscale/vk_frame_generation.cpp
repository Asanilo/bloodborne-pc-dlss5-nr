// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/vk_frame_generation.h"
#ifdef BB_NATIVE_DLSSG
#include "upscale/native_mfg/core.hpp"
#endif
#include "shim/bb_env.h"
#include "bbport_toggles.h"
#include "bbport_timeline.h"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>

#include <vk_mem_alloc.h>

#include "bbport_settings.h"
#include "common/thread.h"
#include "ffx_vk_portable.h"
#include "bbgpu/shaders/fg_display_lut_comp.h"
#include "bbgpu/shaders/fg_cascade_guides_comp.h"
#include "bbgpu/shaders/fg_cascade_hud_comp.h"
#include "render/scheduler.h"
#include "upscale/fg_profile.h"
#include "upscale/ffx_shaders.h"
#include "upscale/vk_fsr_amd.h"
#include "render/vk_gpu_profiler.h"
#include "vulkan/shader_module.h"
#include "vulkan/instance.h"

namespace Vulkan {

namespace {

constexpr vk::ImageSubresourceRange ColorRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
constexpr vk::ImageSubresourceRange DepthRange{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};

struct CascadePush {
    CameraMatrices::Matrix4 current_to_midpoint;
    std::array<float, 2> current_jitter, midpoint_jitter;
};

void BindCascadeImages(vk::CommandBuffer command, vk::Pipeline pipeline, vk::PipelineLayout layout,
                       const std::array<vk::ImageView, 5>& views) {
    std::array<vk::DescriptorImageInfo, 5> images{};
    std::array<vk::WriteDescriptorSet, 5> writes{};
    for (u32 i = 0; i < views.size(); ++i) {
        images[i] = {.imageView = views[i], .imageLayout = vk::ImageLayout::eGeneral};
        writes[i] = {.dstBinding = i,
                     .descriptorCount = 1,
                     .descriptorType = i < 2 ? vk::DescriptorType::eSampledImage
                                             : vk::DescriptorType::eStorageImage,
                     .pImageInfo = &images[i]};
    }
    command.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    command.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout, 0, writes);
}

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
                  vk::AccessFlags2 dst_access, vk::ImageLayout old_layout = vk::ImageLayout::eGeneral,
                  vk::ImageSubresourceRange range = ColorRange) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

/// The real frame again where a generated one could not be made: shown instead of garbage.
void CopyFrame(vk::CommandBuffer cmdbuf, vk::Image from, vk::Image to, u32 width, u32 height) {
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    ImageBarrier(cmdbuf, to, all, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                 vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
    ImageBarrier(cmdbuf, from, all, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                 vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
    cmdbuf.copyImage(from, vk::ImageLayout::eGeneral, to, vk::ImageLayout::eGeneral,
                     vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   .extent = {width, height, 1}});
    ImageBarrier(cmdbuf, to, vk::PipelineStageFlagBits2::eTransfer,
                 vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead);
    ImageBarrier(cmdbuf, from, vk::PipelineStageFlagBits2::eTransfer,
                 vk::AccessFlagBits2::eTransferRead, all,
                 vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
}

const char* ProviderName(int provider) {
    return provider == BbSettings::FrameGenDlss     ? "DLSS Frame Generation"
           : provider == BbSettings::FrameGenFsr3   ? "FSR 3.1 frame generation"
           : provider == BbSettings::FrameGenFsrAmd ? "AMD's frame generation DLL"
                                                    : "none";
}

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
    auto& settings = BbSettings::Get();
    // FSR 3.1 frame interpolation: any GPU with its prerequisites.
    FfxVkPortableDeviceCapabilities caps{};
    caps.structSize = sizeof(caps);
    if (ffxVkPortableQueryDeviceCapabilities(instance.GetPhysicalDevice(), &caps) !=
        FFX_VK_PORTABLE_OK) {
        settings.frame_generation_problem = "the GPU's capabilities could not be read";
    } else if (!caps.fsr3FrameGenerationPrerequisites) {
        settings.frame_generation_problem = "the GPU lacks what FSR 3 frame generation needs";
    } else {
        supported = true;
    }
    settings.frame_generation_supported = supported;
    std::printf("Frame generation: FSR 3.1 %s%s%s\n", supported ? "available" : "unavailable",
                supported ? "" : ": ", supported ? "" : settings.frame_generation_problem.load());
    // DLSS Frame Generation: the bridge, NVIDIA's library, a GeForce RTX 40 or newer.
    const Dlss* dlss = Dlss::Get();
    dlss_max_count = dlss ? std::clamp(dlss->FrameGenMaxCount(), 0, int(MaxGenerated)) : 0;
    static std::string dlss_problem;
    dlss_problem = dlss ? dlss->FrameGenProblem()
                        : "the DLSS bridge and NVIDIA's DLSS libraries are not installed";
    if (dlss_max_count < 1 && dlss_problem.empty()) {
        dlss_problem = "DLSS Frame Generation is unavailable on this system";
    }
#ifdef BB_NATIVE_DLSSG
    if (bb_env_on("BB_DLSSG_DIRECT") && dlss_max_count == 1 &&
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(instance.GetDevice(), "vkCmdCuLaunchKernelNVX") &&
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(instance.GetDevice(), "vkGetImageViewHandle64NVX") &&
        instance.GetPhysicalDevice().getFeatures().shaderStorageImageWriteWithoutFormat) {
        direct_model = Dlss::DirectModelPath();
        direct_enabled = !direct_model.empty();
    }
#endif
    settings.dlssg_max_multiplier = direct_enabled ? int(MaxGenerated + 1) : dlss_max_count + 1;
    if (direct_enabled) {
        std::puts("Frame generation: independent native Ada DLSS-G enabled for 3x-6x (SDK 310.9.1)");
    }
    settings.dlssg_problem = dlss_max_count > 0 ? nullptr : dlss_problem.c_str();
#ifndef _WIN32
    if (!direct_enabled && bb_env_on("BB_DLSSG_CASCADE") && dlss_max_count > 0) {
        const bool bridge = dlss && dlss->SupportsSecondaryFrameGen();
        const bool storage_writes =
            instance.GetPhysicalDevice().getFeatures().shaderStorageImageWriteWithoutFormat;
        cascade_supported = bridge && storage_writes;
        settings.dlssg_cascade_available = cascade_supported;
        settings.dlssg_cascade_problem = !bridge ? "DLSS cascade needs the ABI 4 bridge"
            : !storage_writes ? "DLSS cascade needs format-independent storage image writes"
                              : nullptr;
    }
#endif
    if (dlss_max_count > 0) {
        std::printf("Frame generation: DLSS Frame Generation available, up to %dx\n",
                    dlss_max_count + 1);
    } else {
        std::printf("Frame generation: DLSS Frame Generation unavailable: %s\n",
                    dlss_problem.c_str());
    }
    // AMD's frame generation DLL (Windows, beside the FSR loader).
    amd = std::make_unique<FgAmd>(instance, scheduler);
    settings.fg_amd_supported = amd->Available();
    const int chosen = settings.frame_generation.load();
    if (chosen != BbSettings::FrameGenOff) {
        std::printf("Frame generation: %s chosen, %s\n", ProviderName(chosen),
                    Provider() == chosen ? "on" : "cannot run here: off");
    }
    // Asynchronous FSR 3.1 on the second compute queue (Instance::GetAsyncComputeQueue).
    if (supported && instance.GetAsyncComputeQueue()) {
        const auto device = instance.GetDevice();
        families = {instance.GetGraphicsQueueFamilyIndex(), instance.GetComputeQueueFamilyIndex()};
        vk::SemaphoreTypeCreateInfo timeline{.semaphoreType = vk::SemaphoreType::eTimeline,
                                             .initialValue = 0};
        compute_semaphore = Check(device.createSemaphoreUnique({.pNext = &timeline}));
        compute_pool = Check(device.createCommandPoolUnique({
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = instance.GetComputeQueueFamilyIndex(),
        }));
        const auto cmdbufs = Check(device.allocateCommandBuffers({
            .commandPool = *compute_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = u32(compute_cmdbufs.size()),
        }));
        std::ranges::copy(cmdbufs, compute_cmdbufs.begin());
        async = true;
        worker = std::jthread([this](std::stop_token stop) { WorkerLoop(stop); });
        std::printf("Frame generation: FSR 3.1 asynchronous (compute queue family %u)\n",
                    families[1]);
    }
}

FrameGeneration::~FrameGeneration() {
    WaitAsyncIdle();
    if (worker.joinable()) {
        worker.request_stop();
        job_cv.notify_all();
        worker.join();
    }
    Destroy();
}

vk::ImageCreateInfo FrameGeneration::Shared(vk::ImageCreateInfo info) const {
    if (async) {
        info.sharingMode = vk::SharingMode::eConcurrent;
        info.queueFamilyIndexCount = u32(families.size());
        info.pQueueFamilyIndices = families.data();
    }
    return info;
}

void FrameGeneration::WaitAsyncIdle() {
    if (!async) {
        return;
    }
    const u64 value = compute_issued; // the draw thread enqueues; the worker drains through this tick
    if (value != 0) {
        const vk::Semaphore semaphore = *compute_semaphore;
        (void)instance.GetDevice().waitSemaphores(
            {.semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value}, UINT64_MAX);
    }
}

int FrameGeneration::Provider() const {
    const int chosen = BbSettings::Get().frame_generation.load(std::memory_order_relaxed);
    if (chosen == BbSettings::FrameGenDlss) {
        return dlss_max_count > 0 && !dlss_failed.load(std::memory_order_relaxed)
                   ? BbSettings::FrameGenDlss
                   : BbSettings::FrameGenOff;
    }
    if (chosen == BbSettings::FrameGenFsr3) {
        return supported && !fsr_failed.load(std::memory_order_relaxed) ? BbSettings::FrameGenFsr3
                                                                        : BbSettings::FrameGenOff;
    }
    if (chosen == BbSettings::FrameGenFsrAmd) {
        return amd && amd->Available() && !amd_failed.load(std::memory_order_relaxed)
                   ? BbSettings::FrameGenFsrAmd
                   : BbSettings::FrameGenOff;
    }
    return BbSettings::FrameGenOff;
}

bool FrameGeneration::Wanted() const {
    return Provider() != BbSettings::FrameGenOff;
}

bool FrameGeneration::CascadeWanted(int multiplier) const {
    return multiplier == 4 && cascade_supported && !cascade_failed.load(std::memory_order_relaxed);
}

bool FrameGeneration::CascadeActive() const {
    return Provider() == BbSettings::FrameGenDlss &&
           CascadeWanted(BbSettings::Get().frame_generation_multiplier.load(std::memory_order_relaxed));
}

void FrameGeneration::OnDisplayWrite(VAddr address) {
    const int provider = Provider();
    if (last_provider != BbSettings::FrameGenOff) {
        std::scoped_lock lock{targets_mutex};
        if (last_provider != provider) {
            // With FG off the upscaler skips CaptureScene entirely.
            scene_captured = false;
            reset_next = true;
            last_provider = BbSettings::FrameGenOff;
            ready_display = 0;
        } else if (ready_display == address) {
            ready_display = 0; // the unclaimed phases belong to the image being overwritten
        }
    }
    const bool needs_direct = direct_enabled && provider == BbSettings::FrameGenDlss &&
        BbSettings::Get().frame_generation_multiplier.load(std::memory_order_relaxed) > 2;
    if ((using_direct && !needs_direct) || (cascade_guides[0].image && !CascadeActive())) {
        Destroy();
        scene_captured = false;
        reset_next = true;
    }
}

void FrameGeneration::FailCascade(const char* reason) {
    if (!cascade_failed.exchange(true, std::memory_order_relaxed)) {
        auto& settings = BbSettings::Get();
        settings.dlssg_cascade_available = false;
        settings.dlssg_cascade_problem = reason;
        std::printf("Frame generation: %s; DLSS cascade off\n", reason);
    }
}

u32 FrameGeneration::WantedCount() const {
    return CountFor(Provider(), BbSettings::Get().frame_generation_multiplier.load(std::memory_order_relaxed));
}

u32 FrameGeneration::CountFor(int provider, int multiplier) const {
    switch (provider) {
    case BbSettings::FrameGenDlss: {
        if (using_direct && multiplier > 2) {
            return u32(std::clamp(multiplier - 1, 2, int(MaxGenerated)));
        }
        if (CascadeWanted(multiplier)) {
            return CascadeGenerated;
        }
        return u32(std::clamp(multiplier - 1, 1, std::max(1, dlss_max_count)));
    }
    case BbSettings::FrameGenFsr3:
    case BbSettings::FrameGenFsrAmd:
        return 1;
    default:
        return 0;
    }
}

void FrameGeneration::Destroy() {
    {
        std::scoped_lock lock{targets_mutex};
        ready_display = 0; // a new context must never claim a batch published by the old one
    }
    cascade_history.reset();
    if (context) {
        WaitAsyncIdle();
        scheduler.Finish();
        ffxVkPortableFrameGenerationContextDestroy(context);
        context = nullptr;
    }
    if (dlss_desc) {
        WaitAsyncIdle();
        scheduler.Finish(); // the recorded generations are done: the feature is free
        if (Dlss* dlss = Dlss::Get()) {
            dlss->ReleaseFrameGen();
        }
#ifdef BB_NATIVE_DLSSG
        direct_core.reset();
        direct_upload_tick = 0;
#endif
        const bool retire_targets = using_direct || bool(cascade_guides[0].image);
        using_direct = false;
        last_direct_source = last_direct_present = 0;
        dlss_desc.reset();
        dlss_create_pending = false;
        if (cascade_guides[0].image) {
            for (auto& guide : cascade_guides) {
                guide.view.reset();
                guide.image.Destroy();
            }
            cascade_skip = {};
        }
        if (retire_targets) {
            std::scoped_lock lock{targets_mutex};
            for (auto& target : targets) {
                target.view.reset();
                target.image.Destroy();
                target.ready = false;
                target.disable_interpolation = nullptr;
            }
        }
    }
    if (amd) {
        amd->Destroy();
    }
}

bool FrameGeneration::EnsureAmd(u32 render_width, u32 render_height, u32 width, u32 height,
                                vk::Format format) {
    if (context || dlss_desc) {
        Destroy(); // the other provider's (the GPU is done with it)
    }
    bool created = false;
    static std::string problem; // read by the menu
    std::string why;
    if (!amd->Ensure(render_width, render_height, width, height, format, created, why)) {
        problem = "AMD's DLL: " + why;
        Fail(BbSettings::FrameGenFsrAmd, problem.c_str());
        return false;
    }
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    if (created) {
        reset_next = true;
    }
    return true;
}

void FrameGeneration::RecordAmd(vk::Image color, u64 id, bool reset, Target& target) {
    FgAmd::Frame f{};
    using Image = Fsr4Upscaler::Image;
    f.color = Image{.image = color, .width = scene.width, .height = scene.height,
                    .format = scene.color_format};
    if (hudless_ready && !BbToggle::Disabled(1u << 30)) {
        f.hudless = Image{.image = vk::Image(In().hudless), .width = scene.width,
                          .height = scene.height, .format = scene.color_format};
    }
    hudless_ready = false;
    f.depth = Image{.image = vk::Image(In().depth_copy), .width = scene.render_width,
                    .height = scene.render_height, .format = scene.depth_format};
    f.motion = Image{.image = scene.motion, .width = scene.render_width,
                     .height = scene.render_height, .format = vk::Format::eR16G16Sfloat};
    f.output = Image{.image = vk::Image(target.image), .width = target.width,
                     .height = target.height, .format = target.format};
    f.jitter[0] = scene.jitter_x;
    f.jitter[1] = scene.jitter_y;
    f.frame_ms = scene.frame_ms;
    f.near_plane = scene.camera_near;
    f.far_plane = scene.camera_far;
    f.vertical_fov = scene.vertical_fov;
    std::copy_n(scene.camera_position, 3, f.camera_position);
    std::copy_n(scene.camera_up, 3, f.camera_up);
    std::copy_n(scene.camera_right, 3, f.camera_right);
    std::copy_n(scene.camera_forward, 3, f.camera_forward);
    f.reset = reset;
    f.frame_id = id;
    scheduler.EndRendering();
    scheduler.Record([this, f](vk::CommandBuffer cmdbuf) mutable {
        const vk::Image color = f.color.image, out = f.output.image;
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                     vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
        ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryRead,
                     vk::PipelineStageFlagBits2::eComputeShader,
                     vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eUndefined);
        f.cmdbuf = cmdbuf;
        FgProfile::Begin(instance, cmdbuf);
        std::string problem;
        const bool ok = amd->Record(f, problem);
        FgProfile::End(cmdbuf);
        if (!ok) {
            static int told = 0;
            if (told++ < 4) {
                std::printf("Frame generation: AMD's DLL: %s\n", problem.c_str());
            }
        }
        // AMD's passes leave their own barriers behind; everything after waits for them.
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryRead, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryWrite, all,
                     vk::AccessFlagBits2::eMemoryRead);
        if (!ok) {
            CopyFrame(cmdbuf, color, out, f.output.width, f.output.height);
        }
    });
}

bool FrameGeneration::EnsureContext(u32 render_width, u32 render_height, u32 width, u32 height,
                                    vk::Format format) {
    if (context && context_render_w == render_width && context_render_h == render_height &&
        context_w == width && context_h == height && context_format == format) {
        return true;
    }
    Destroy();
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
        Fail(BbSettings::FrameGenFsr3, "FSR 3 frame generation: invalid context");
        return false;
    }
    FfxShaders::Install(); // passes built in the port, before the pipelines are made
    if (ffxVkPortableFrameGenerationContextCreate(&device_info, &create, &context) !=
        FFX_VK_PORTABLE_OK) {
        context = nullptr;
        Fail(BbSettings::FrameGenFsr3, "FSR 3 frame generation: the context could not be made");
        return false;
    }
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    reset_next = true;
    std::printf("Frame generation: FSR 3.1 %ux%u from %ux%u scenes (%s)\n", width, height,
                render_width, render_height, vk::to_string(format).c_str());
    return true;
}

bool FrameGeneration::EnsureDlss(u32 render_width, u32 render_height, u32 width, u32 height,
                                 vk::Format format, vk::Format depth_format) {
    const Dlss::FrameGenDesc desc{width, height, format, render_width, render_height};
    direct_profile = direct_enabled && width == 1920 && height == 1080 &&
        render_width == 1280 && render_height == 720 && format == vk::Format::eR8G8B8A8Unorm &&
        depth_format == vk::Format::eD32SfloatS8Uint;
    BbSettings::Get().dlssg_max_multiplier = direct_profile ? int(MaxGenerated + 1) : dlss_max_count + 1;
    const int multiplier = BbSettings::Get().frame_generation_multiplier.load(std::memory_order_relaxed);
    const bool direct = direct_profile && multiplier > 2;
    if (dlss_desc && *dlss_desc == desc && using_direct == direct) {
        return true;
    }
    Destroy(); // the FSR context or the previous feature (the GPU is done with them)
    if (direct_enabled && multiplier > 2 && !direct_profile) {
        std::printf("DLSS-G direct: %ux%u from %ux%u is outside the 1080p Quality profile; using stock 2x.\n",
                    width, height, render_width, render_height);
    }
    dlss_desc = desc;
    using_direct = direct;
    dlss_create_pending = true; // made in the next recorded generation (it needs a command buffer)
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    reset_next = true;
    std::printf("Frame generation: DLSS %ux%u from %ux%u scenes (%s), %ux\n", width, height,
                render_width, render_height, vk::to_string(format).c_str(), WantedCount() + 1);
    return true;
}

void FrameGeneration::CaptureScene(const Scene& in) {
    scene_captured = false;
    const int provider = Provider();
    if (provider == BbSettings::FrameGenOff) {
        reset_next = true;
        cascade_history.reset();
        return;
    }
    const bool ready = provider == BbSettings::FrameGenDlss
                           ? EnsureDlss(in.render_width, in.render_height, in.width, in.height,
                                        in.color_format, in.depth_format)
                       : provider == BbSettings::FrameGenFsrAmd
                           ? EnsureAmd(in.render_width, in.render_height, in.width, in.height,
                                       in.color_format)
                           : EnsureContext(in.render_width, in.render_height, in.width, in.height,
                                           in.color_format);
    if (!ready) {
        reset_next = true;
        return;
    }
    if (provider != last_provider) {
        last_provider = provider; // the new provider has no history
        reset_next = true;
    }
    scene = in;
    scene_captured = true;
    scene_id = ++frame_id;
    hudless_ready = false;
    scene_reset = reset_next || in.reset;
    reset_next = false;
    ++stat_captured;
    // Asynchronous: the other set of inputs than the last frame's, once the interpolation that
    // read it (two frames ago, done by now as a rule) has finished.
    in_slot = async && provider == BbSettings::FrameGenFsr3 ? u32(scene_id & 1) : 0;
    if (const u64 value = In().compute_value; value != 0) {
        const vk::Semaphore semaphore = *compute_semaphore;
        (void)instance.GetDevice().waitSemaphores(
            {.semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value}, UINT64_MAX);
    }
    // The frame is interpolated as shown, UI and all, with the frame before its UI beside it
    // when there is one (CaptureHudless). The generation runs at the display pass; the depth is
    // copied now, while it is this frame's (the game reuses its depth buffer).
    if (!In().depth_copy || In().depth_copy_format != in.depth_format || In().depth_copy_w != in.render_width ||
        In().depth_copy_h != in.render_height) {
        WaitAsyncIdle();
        scheduler.Finish();
        In().depth_copy_view.reset();
        In().depth_copy = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        In().depth_copy.Create(Shared(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = in.depth_format,
            .extent = {in.render_width, in.render_height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
        // DLSS samples the depth through a view of its depth aspect.
        In().depth_copy_view = Check<"create a frame generation depth view">(
            instance.GetDevice().createImageViewUnique({
                .image = vk::Image(In().depth_copy),
                .viewType = vk::ImageViewType::e2D,
                .format = in.depth_format,
                .subresourceRange = DepthRange,
            }));
        In().depth_copy_format = in.depth_format;
        In().depth_copy_w = in.render_width;
        In().depth_copy_h = in.render_height;
    }
    scheduler.Record([depth = in.depth, copy = vk::Image(In().depth_copy), w = in.render_width,
                      h = in.render_height](vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
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
             .image = copy, .subresourceRange = range},
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = before.data()});
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
            .extent = {w, h, 1},
        };
        cmdbuf.copyImage(depth, vk::ImageLayout::eGeneral, copy, vk::ImageLayout::eGeneral, region);
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
             .image = copy, .subresourceRange = range},
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = after.data()});
    });
}

void FrameGeneration::CaptureHudless(vk::Image color) {
    if (!scene_captured || color != scene.color || hudless_ready) {
        return;
    }
    if (!In().hudless || In().hudless.image_ci.format != context_format ||
        In().hudless.image_ci.extent.width != context_w || In().hudless.image_ci.extent.height != context_h) {
        WaitAsyncIdle();
        scheduler.Finish();
        In().hudless_view.reset();
        In().hudless = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        In().hudless.Create(Shared(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = context_format,
            .extent = {context_w, context_h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = ColorUsage,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
        In().hudless_view = Check<"create a frame generation HUD-less view">(
            instance.GetDevice().createImageViewUnique({
                .image = vk::Image(In().hudless),
                .viewType = vk::ImageViewType::e2D,
                .format = context_format,
                .subresourceRange = ColorRange,
            }));
    }
    hudless_ready = true;
    scheduler.EndRendering();
    scheduler.Record([color, copy = vk::Image(In().hudless), w = scene.width,
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

void FrameGeneration::EnsureTarget(Target& target) {
    if (!target.image || target.format != scene.color_format || target.width != scene.width ||
        target.height != scene.height) {
        if (target.image) {
            if (target.compute_value) {
                const auto semaphore = *compute_semaphore;
                Check(instance.GetDevice().waitSemaphores(
                    {.semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &target.compute_value}, UINT64_MAX));
                if (target.release_value) {
                    Check(instance.GetDevice().waitSemaphores(
                        {.semaphoreCount = 1, .pSemaphores = &target.release_semaphore,
                         .pValues = &target.release_value}, UINT64_MAX));
                }
            } else {
                scheduler.Finish(); // synchronous encode and all its views are done
            }
        }
        const auto device = instance.GetDevice();
        target.view.reset();
        target.image = VideoCore::HostImage(device, instance.GetAllocator());
        target.image.Create(Shared(vk::ImageCreateInfo{
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
        }));
        target.view = Check<"create a frame generation view">(device.createImageViewUnique({
            .image = vk::Image(target.image),
            .viewType = vk::ImageViewType::e2D,
            .format = scene.color_format,
            .subresourceRange = ColorRange,
        }));
        target.format = scene.color_format;
        target.width = scene.width;
        target.height = scene.height;
    }
}

vk::ImageView FrameGeneration::ColorView(vk::Image image, vk::Format format) {
    if (color_view && color_viewed == image && color_view_format == format) {
        return *color_view;
    }
    WaitAsyncIdle();
    scheduler.Finish(); // the last generation may still read through the old view
    color_view = Check<"create a frame generation colour view">(
        instance.GetDevice().createImageViewUnique({
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = ColorRange,
        }));
    color_viewed = image;
    color_view_format = format;
    return *color_view;
}

void FrameGeneration::Interpolate(vk::Image color, VAddr display) {
    Stats();
    if (!scene_captured || color != scene.color) {
        return;
    }
    const int provider = last_provider;
    if ((provider == BbSettings::FrameGenFsr3 && !context) ||
        (provider == BbSettings::FrameGenDlss && !dlss_desc) ||
        (provider == BbSettings::FrameGenFsrAmd && !amd->Ready())) {
        return;
    }
    scene_captured = false;
    const int requested = BbSettings::Get().frame_generation_multiplier.load(std::memory_order_relaxed);
    if (Provider() != provider || (provider == BbSettings::FrameGenDlss &&
                                  using_direct != (direct_profile && requested > 2))) {
        reset_next = true;
        return; // changed between capture and display
    }
    const bool cascade = provider == BbSettings::FrameGenDlss && CascadeWanted(requested);
    const u32 count = CountFor(provider, requested);
    ++stat_interpolated;
    const u64 id = scene_id;
    bool reset = scene_reset;
    CameraMatrices::Pose midpoint{}, previous_pose{};
    std::array<float, 2> midpoint_jitter{};
    if (using_direct) {
        const u64 presented = BbStats::frame_number.load(std::memory_order_relaxed);
        if (id != last_direct_source + 1 || presented > last_direct_present + 1) reset = true;
        last_direct_source = id;
        last_direct_present = presented;
    }
    if (cascade) {
        const u64 source = BbStats::frame_number.load(std::memory_order_relaxed);
        // A missing display can leave a gap in the capture ids. When the presenter has
        // caught up, its real-frame counter also detects intervening frames with no capture.
        if (cascade_history && id == cascade_history->capture + 1 &&
            source <= cascade_history->source + 1 && scene.has_matrices && !reset) {
            previous_pose = cascade_history->pose;
            midpoint = CameraMatrices::Midpoint(previous_pose, scene.camera_pose);
            midpoint_jitter = {std::midpoint(cascade_history->jitter[0], scene.jitter_x),
                               std::midpoint(cascade_history->jitter[1], scene.jitter_y)};
        }
        if (!cascade_history) {
            std::printf("Frame generation: DLSS cascade 4x (two independent 2x features)\n");
        }
        reset = !midpoint.valid;
        cascade_history = CascadeHistory{scene.camera_pose, {scene.jitter_x, scene.jitter_y},
                                         id, source};
        EnsureCascadeResources();
    } else if (cascade_history) {
        // The primary feature's history was HUD-less in cascade mode.
        cascade_history.reset();
        reset = true;
    }
    if (async && provider == BbSettings::FrameGenFsr3) {
        if (!reset) ++stat_ready;
        InterpolateAsync(color, display, id, reset);
        return;
    }
    for (u32 i = 0; i < count; ++i) EnsureTarget(targets[i]);
    {
        std::scoped_lock lock{targets_mutex};
        ready_display = reset ? 0 : display;
        for (u32 i = 0; i < MaxGenerated; ++i) {
            Target& target = targets[i];
            // Ready as recorded (the presenter's encode comes later in the same stream). The
            // first frame after a reset has nothing before it to interpolate from.
            target.ready = i < count && !reset;
            target.frame = id;
            target.disable_interpolation = cascade && i < CascadeGenerated ? cascade_skip[i]->Handle() : vk::Buffer{};
        }
        if (!reset) ++stat_ready;
    }
    // Its own mark: the GPU profile otherwise counted it to the game's last dispatch.
    if (auto* profiler = GpuProfiler::Get()) {
        const char* label = using_direct ? "frame generation (DLSS-G direct)"
                            : cascade ? "frame generation (DLSS cascade)"
                            : provider == BbSettings::FrameGenDlss ? "frame generation (DLSS)"
                            : provider == BbSettings::FrameGenFsrAmd
                                ? "frame generation (AMD's DLL)"
                                : "frame generation (FSR 3.1)";
        profiler->Mark(0xF6A0'0000ull ^ std::hash<std::string_view>{}(label),
                       [label] { return std::string{label}; });
    }
    if (provider == BbSettings::FrameGenDlss) {
        RecordDlss(color, reset, count, cascade, midpoint, midpoint_jitter, previous_pose);
    } else if (provider == BbSettings::FrameGenFsrAmd) {
        RecordAmd(color, id, reset, targets[0]);
    } else {
        RecordFsr(color, id, reset, targets[0]);
    }
}

FrameGeneration::Job FrameGeneration::BuildFsr(vk::Image color, vk::Image motion, u64 id,
                                               bool reset, Target& target) {
    Job job{};
    auto& dispatch = job.dispatch;
    dispatch.structSize = sizeof(dispatch);
    dispatch.currentColor = Describe(color, scene.color_format, scene.width, scene.height,
                                     ColorUsage | vk::ImageUsageFlagBits::eColorAttachment,
                                     vk::ImageAspectFlagBits::eColor,
                                     FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    // The UI-less frame (CaptureHudless): FSR interpolates it and lays the UI over unmoved. Without
    // one (no UI drawn this frame) the whole frame is interpolated.
    if (hudless_ready && !BbToggle::Disabled(1u << 30)) {
        dispatch.hudlessColor = Describe(vk::Image(In().hudless), scene.color_format, scene.width,
                                         scene.height, ColorUsage, vk::ImageAspectFlagBits::eColor,
                                         FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    } else {
        dispatch.hudlessColor.structSize = sizeof(dispatch.hudlessColor); // none: described, null
    }
    hudless_ready = false;
    dispatch.distortionField.structSize = sizeof(dispatch.distortionField); // absent: described, null
    dispatch.output = Describe(vk::Image(target.image), scene.color_format, scene.width,
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
    auto& prepare = job.prepare;
    prepare.structSize = sizeof(prepare);
    prepare.depth = Describe(vk::Image(In().depth_copy), scene.depth_format, scene.render_width,
                             scene.render_height, vk::ImageUsageFlagBits::eSampled,
                             vk::ImageAspectFlagBits::eDepth, FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.motionVectors = Describe(motion, vk::Format::eR16G16Sfloat, scene.render_width,
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
    job.source =
        dispatch.hudlessColor.image != VK_NULL_HANDLE ? dispatch.hudlessColor : dispatch.currentColor;
    job.color = color;
    job.out = vk::Image(target.image);
    job.width = target.width;
    job.height = target.height;
    return job;
}

void FrameGeneration::RecordJob(vk::CommandBuffer cmdbuf, Job& job) {
    const vk::Image color = job.color, out = job.out;
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    // The UI drew into it; the display pass reads it after.
    ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                 vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
    ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryRead,
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eUndefined);
    job.prepare.commandBuffer = cmdbuf;
    job.dispatch.commandBuffer = cmdbuf;
    FgProfile::Begin(instance, cmdbuf);
    const auto prepared = ffxVkPortableFrameGenerationContextPrepare(context, &job.prepare, &job.source);
    const auto dispatched = prepared == FFX_VK_PORTABLE_OK
                                ? ffxVkPortableFrameGenerationContextRecordDispatch(context, &job.dispatch)
                                : prepared;
    FgProfile::End(cmdbuf);
    const bool ok = dispatched == FFX_VK_PORTABLE_OK;
    if (!ok) {
        static int told = 0;
        if (told++ < 4) {
            std::printf("Frame generation: the interpolation could not be recorded (%s %d)\n",
                        prepared != FFX_VK_PORTABLE_OK ? "prepare" : "dispatch", int(dispatched));
        }
    }
    ImageBarrier(cmdbuf, color, vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderRead, all,
                 vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
    ImageBarrier(cmdbuf, out, vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderStorageWrite, all, vk::AccessFlagBits2::eMemoryRead);
    if (!ok) {
        // Shown anyway (it was handed out as ready): the real frame twice, not garbage.
        CopyFrame(cmdbuf, color, out, job.width, job.height);
    }
}

void FrameGeneration::RecordFsr(vk::Image color, u64 id, bool reset, Target& target) {
    Job job = BuildFsr(color, scene.motion, id, reset, target);
    scheduler.EndRendering();
    scheduler.Record([this, job](vk::CommandBuffer cmdbuf) mutable { RecordJob(cmdbuf, job); });
}

void FrameGeneration::InterpolateAsync(vk::Image color, VAddr display, u64 id, bool reset) {
    Inputs& in = In();
    // Copies of the frame and its motion vectors (both reused by the next frame), in this set.
    const auto ensure = [&](VideoCore::HostImage& image, vk::Format format, u32 w, u32 h,
                            vk::ImageUsageFlags usage, vk::ImageCreateFlags flags) {
        if (image && image.image_ci.format == format && image.image_ci.extent.width == w &&
            image.image_ci.extent.height == h) {
            return;
        }
        WaitAsyncIdle();
        scheduler.Finish();
        image = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        image.Create(Shared(vk::ImageCreateInfo{
            .flags = flags,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {w, h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
    };
    ensure(in.color_copy, scene.color_format, scene.width, scene.height,
           ColorUsage | vk::ImageUsageFlagBits::eColorAttachment, {});
    ensure(in.motion_copy, vk::Format::eR16G16Sfloat, scene.render_width, scene.render_height,
           vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eTransferDst,
           {});
    Target* target = nullptr;
    {
        std::scoped_lock lock{targets_mutex};
        ready_display = 0;
        for (u32 i = 0; i < async_targets.size(); ++i) {
            ready_async_slot = next_target++ % async_targets.size();
            auto& candidate = async_targets[ready_async_slot];
            if (candidate.handed == candidate.released) {
                target = &candidate;
                break;
            }
        }
    }
    if (!target) {
        reset_next = true; // all outputs are still lent out; the real frame can proceed
        return;
    }
    EnsureTarget(*target);
    Job job = BuildFsr(vk::Image(in.color_copy), vk::Image(in.motion_copy), id, reset, *target);
    job.reader_semaphore = target->release_semaphore;
    job.reader_value = target->release_value;
    scheduler.EndRendering();
    scheduler.Record([color, motion = scene.motion, color_copy = vk::Image(in.color_copy),
                      motion_copy = vk::Image(in.motion_copy), w = scene.width, h = scene.height,
                      rw = scene.render_width, rh = scene.render_height](vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const auto copy = [&](vk::Image from, vk::Image to, u32 width, u32 height) {
            ImageBarrier(cmdbuf, from, all, vk::AccessFlagBits2::eMemoryWrite,
                         vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
            ImageBarrier(cmdbuf, to, all, vk::AccessFlagBits2::eNone,
                         vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                         vk::ImageLayout::eUndefined);
            cmdbuf.copyImage(from, vk::ImageLayout::eGeneral, to, vk::ImageLayout::eGeneral,
                             vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                           .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                           .extent = {width, height, 1}});
            ImageBarrier(cmdbuf, from, vk::PipelineStageFlagBits2::eCopy,
                         vk::AccessFlagBits2::eTransferRead, all,
                         vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
            ImageBarrier(cmdbuf, to, vk::PipelineStageFlagBits2::eCopy,
                         vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead);
        };
        copy(color, color_copy, w, h);
        copy(motion, motion_copy, rw, rh);
    });
    // The frame goes out now; the interpolation waits for it on the compute queue.
    job.graphics_tick = scheduler.CurrentTick();
    scheduler.Flush();
    {
        std::scoped_lock lock{job_mutex};
        job.value = ++compute_issued;
        in.compute_value = job.value;
        jobs.push_back(job);
    }
    job_cv.notify_all();
    {
        std::scoped_lock lock{targets_mutex};
        target->frame = id;
        target->compute_value = job.value;
        ready_display = reset ? 0 : display;
    }
}

void FrameGeneration::WorkerLoop(std::stop_token stop) {
    Common::SetCurrentThreadName("bb:FrameGen");
    const auto device = instance.GetDevice();
    const vk::Semaphore compute = *compute_semaphore;
    const vk::Semaphore graphics = scheduler.GetWorkSemaphore()->Handle();
    while (true) {
        Job job;
        {
            std::unique_lock lock{job_mutex};
            if (!job_cv.wait(lock, stop, [this] { return !jobs.empty(); })) {
                return;
            }
            job = jobs.front();
            jobs.pop_front();
        }
        // The command buffer of three jobs ago is done with.
        const vk::CommandBuffer cmdbuf = compute_cmdbufs[job.value % compute_cmdbufs.size()];
        if (job.value > compute_cmdbufs.size()) {
            const u64 done = job.value - compute_cmdbufs.size();
            (void)device.waitSemaphores({.semaphoreCount = 1, .pSemaphores = &compute, .pValues = &done},
                                        UINT64_MAX);
        }
        Check(cmdbuf.reset());
        Check(cmdbuf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
        RecordJob(cmdbuf, job);
        Check(cmdbuf.end());
        // The frame's submission first in its queue (a timeline wait may come before its signal,
        // but not on every driver path).
        scheduler.WaitSubmitted(job.graphics_tick);
        std::array<vk::SemaphoreSubmitInfo, 2> waits{{
            {.semaphore = graphics, .value = job.graphics_tick,
             .stageMask = vk::PipelineStageFlagBits2::eAllCommands},
            {.semaphore = job.reader_semaphore, .value = job.reader_value,
             .stageMask = vk::PipelineStageFlagBits2::eAllCommands},
        }};
        const vk::SemaphoreSubmitInfo signal{
            .semaphore = compute, .value = job.value,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
        const vk::CommandBufferSubmitInfo cmd_info{.commandBuffer = cmdbuf};
        const vk::SubmitInfo2 submit{
            .waitSemaphoreInfoCount = job.reader_semaphore ? 2u : 1u,
            .pWaitSemaphoreInfos = waits.data(),
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signal,
        };
        {
            std::scoped_lock lock{instance.AsyncComputeMutex(), Scheduler::submit_mutex};
            Check<"submit the frame generation">(instance.GetAsyncComputeQueue().submit2(submit));
        }
    }
}

void FrameGeneration::ReleaseOutput(u32 target, u64 frame, vk::Semaphore semaphore, u64 value) {
    std::scoped_lock lock{targets_mutex};
    Target& slot = async_targets.at(target);
    if (slot.handed != frame || slot.released == frame) return;
    slot.released = frame;
    if (value) {
        slot.release_semaphore = semaphore;
        slot.release_value = value;
    }
}

void FrameGeneration::EnsureDlssOverlayPipelines(bool need_guides) {
    const auto device = instance.GetDevice();
    if (!cascade_layout) {
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
        for (u32 i = 0; i < bindings.size(); ++i) {
            bindings[i] = {.binding = i,
                           .descriptorType = i < 2 ? vk::DescriptorType::eSampledImage
                                                   : vk::DescriptorType::eStorageImage,
                           .descriptorCount = 1,
                           .stageFlags = vk::ShaderStageFlagBits::eCompute};
        }
        cascade_layout = Check(device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            .bindingCount = u32(bindings.size()), .pBindings = bindings.data()}));
        const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, sizeof(CascadePush)};
        cascade_pipeline_layout = Check(device.createPipelineLayoutUnique({
            .setLayoutCount = 1, .pSetLayouts = &*cascade_layout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push}));
    }
    const auto pipeline = [&](auto& code) {
        const auto module = CompileSPV(code, device);
        auto result = Check(device.createComputePipelineUnique(
            {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module,
                          .pName = "main"},
                .layout = *cascade_pipeline_layout}));
        device.destroyShaderModule(module);
        return result;
    };
    if (need_guides && !cascade_guides_pipeline) cascade_guides_pipeline = pipeline(FG_CASCADE_GUIDES_COMP);
    if (!cascade_hud_pipeline) cascade_hud_pipeline = pipeline(FG_CASCADE_HUD_COMP);
}

void FrameGeneration::EnsureCascadeResources() {
    EnsureDlssOverlayPipelines(true);
    const auto device = instance.GetDevice();
    if (cascade_guides[0].image) {
        return; // Destroy retires the resources when the feature's dimensions change.
    }
    scheduler.EndRendering();
    for (u32 i = 0; i < cascade_guides.size(); ++i) {
        if (!cascade_skip[i]) {
            cascade_skip[i] = std::make_unique<VideoCore::Buffer>(
                instance, 0, sizeof(u32), VideoCore::MemoryType::DeviceLocal, "DLSS skip flag");
        }
        auto& guide = cascade_guides[i];
        const auto format = i == 0 ? vk::Format::eR32Sfloat : vk::Format::eR16G16Sfloat;
        guide.image = VideoCore::HostImage(device, instance.GetAllocator());
        guide.image.Create({
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {scene.render_width, scene.render_height, 1},
            .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        const vk::Image image = guide.image;
        guide.view = Check(device.createImageViewUnique({
            .image = image, .viewType = vk::ImageViewType::e2D, .format = format,
            .subresourceRange = ColorRange}));
        scheduler.Record([image](vk::CommandBuffer cmdbuf) {
            ImageBarrier(cmdbuf, image, {}, {}, vk::PipelineStageFlagBits2::eComputeShader,
                         vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eUndefined);
        });
    }
}

void FrameGeneration::RecordCascadeGuides(vk::CommandBuffer cmdbuf,
                                         const Dlss::FrameGenFrame& frame,
                                         const Dlss::FrameGenFrame& midpoint,
                                         const Dlss::FrameGenFrame& half) {
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    const auto compute = vk::PipelineStageFlagBits2::eComputeShader;
    const auto write = vk::AccessFlagBits2::eShaderStorageWrite;
    for (const vk::Image image : {midpoint.depth.image, midpoint.motion.image, half.motion.image}) {
        ImageBarrier(cmdbuf, image, all, vk::AccessFlagBits2::eMemoryRead, compute, write);
    }
    BindCascadeImages(cmdbuf, *cascade_guides_pipeline, *cascade_pipeline_layout,
                      {frame.depth.view, frame.motion.view, midpoint.depth.view,
                       midpoint.motion.view, half.motion.view});
    const CascadePush push{half.clip_to_prev_clip, {frame.jitter_x, frame.jitter_y},
                           {midpoint.jitter_x, midpoint.jitter_y}};
    cmdbuf.pushConstants(*cascade_pipeline_layout, vk::ShaderStageFlagBits::eCompute,
                         0, sizeof(push), &push);
    cmdbuf.dispatch((frame.depth.width + 7) / 8, (frame.depth.height + 7) / 8, 1);
    for (const vk::Image image : {midpoint.depth.image, midpoint.motion.image, half.motion.image}) {
        ImageBarrier(cmdbuf, image, compute, write, compute, vk::AccessFlagBits2::eShaderRead);
    }
}

void FrameGeneration::RecordDlssHud(vk::CommandBuffer cmdbuf, const Dlss::Resource& color,
                                    const Dlss::Resource& hudless, std::span<const Dlss::Resource> outputs) {
    for (size_t i = 0; i < outputs.size(); i += 3) {
        const size_t last = outputs.size() - 1;
        const u32 slots = u32(std::min<size_t>(3, outputs.size() - i));
        cmdbuf.pushConstants(*cascade_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(slots), &slots);
        BindCascadeImages(cmdbuf, *cascade_hud_pipeline, *cascade_pipeline_layout,
            {color.view, hudless.view, outputs[i].view, outputs[std::min(last, i + 1)].view,
             outputs[std::min(last, i + 2)].view});
        cmdbuf.dispatch((color.width + 7) / 8, (color.height + 7) / 8, 1);
    }
}

void FrameGeneration::RecordDlssCascade(
    const Dlss::FrameGenDesc& desc, bool create, Dlss::FrameGenFrame frame,
    const std::array<Dlss::Resource, MaxGenerated>& outputs, const CameraMatrices::Pose& midpoint,
    const std::array<float, 2>& midpoint_jitter, const CameraMatrices::Pose& previous_pose) {
    const auto color = frame.backbuffer;
    const auto hudless = frame.hudless;
    if (hudless.image) {
        frame.backbuffer = hudless;
        frame.hudless = {};
    }
    frame.count = frame.index = 1; // Each feature uses publicly supported ordinary 2x.
    frame.output = outputs[1];

    Dlss::FrameGenFrame mid = frame, half = frame;
    if (!frame.reset) {
        const auto guide = [&](u32 i, vk::Format format) {
            return Dlss::Resource{vk::Image(cascade_guides[i].image), *cascade_guides[i].view,
                                  format, vk::ImageAspectFlagBits::eColor,
                                  desc.render_width, desc.render_height};
        };
        mid.backbuffer = outputs[1];
        mid.depth = guide(0, vk::Format::eR32Sfloat);
        mid.motion = guide(1, vk::Format::eR16G16Sfloat);
        mid.output = outputs[0];
        mid.jitter_x = midpoint_jitter[0];
        mid.jitter_y = midpoint_jitter[1];
        mid.camera_near = -midpoint.projection[3] / midpoint.projection[2];
        mid.camera_fov = 2.0f * std::atan(1.0f / std::abs(midpoint.projection[1]));
        const float forward_sign = mid.camera_near > 0.0f ? 1.0f : -1.0f;
        for (int row = 0; row < 3; ++row) {
            mid.camera_right[row] = midpoint.inv_view[row * 4];
            mid.camera_up[row] = midpoint.inv_view[row * 4 + 1];
            mid.camera_forward[row] = forward_sign * midpoint.inv_view[row * 4 + 2];
            mid.camera_position[row] = midpoint.inv_view[row * 4 + 3];
        }
        mid.has_matrices = CameraMatrices::ClipTransforms(
            midpoint.view, midpoint.inv_view, midpoint.projection, previous_pose.view,
            previous_pose.inv_view, previous_pose.projection, mid.view_to_clip, mid.clip_to_prev_clip,
            mid.prev_clip_to_clip);
        half.motion = guide(2, vk::Format::eR16G16Sfloat);
        half.has_matrices = CameraMatrices::ClipTransforms(
            scene.camera_pose.view, scene.camera_pose.inv_view, scene.camera_pose.projection,
            midpoint.view, midpoint.inv_view, midpoint.projection, half.view_to_clip,
            half.clip_to_prev_clip, half.prev_clip_to_clip);
    }
    half.output = outputs[2];

    frame.disable_interpolation = cascade_skip[1]->Handle();
    mid.disable_interpolation = cascade_skip[0]->Handle();
    half.disable_interpolation = cascade_skip[2]->Handle();

    scheduler.EndRendering();
    // Record stateful NGX evaluations in source order on the command producer.
    const auto cmdbuf = scheduler.CommandBuffer();
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    const auto compute = vk::PipelineStageFlagBits2::eComputeShader;
    const auto read = vk::AccessFlagBits2::eShaderRead;
    const auto write = vk::AccessFlagBits2::eShaderStorageWrite;
    const auto memory = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    for (const auto& input : {color, hudless, frame.depth, frame.motion}) {
        if (input.image) {
            ImageBarrier(cmdbuf, input.image, all, vk::AccessFlagBits2::eMemoryWrite,
                         compute, read, vk::ImageLayout::eGeneral, {input.aspect, 0, 1, 0, 1});
        }
    }
    // PrepareFrame copies the three status words before the next source pair can reuse them.
    const vk::MemoryBarrier2 to_clear{
        .srcStageMask = all, .srcAccessMask = memory,
        .dstStageMask = vk::PipelineStageFlagBits2::eClear,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
    };
    cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &to_clear});
    for (const auto& flag : cascade_skip) {
        cmdbuf.fillBuffer(flag->Handle(), 0, 4, 1);
    }
    const vk::MemoryBarrier2 to_ngx{
        .srcStageMask = vk::PipelineStageFlagBits2::eClear,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = compute, .dstAccessMask = write,
    };
    cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &to_ngx});
    // A successful NGX Evaluate can skip writing, such as when it detects a cut.
    // Prefill the unadorned inputs so all three outputs remain defined in that case.
    for (const auto& output : std::span(outputs).first(CascadeGenerated)) {
        ImageBarrier(cmdbuf, output.image, all, {}, vk::PipelineStageFlagBits2::eTransfer,
                     vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eUndefined);
        CopyFrame(cmdbuf, frame.backbuffer.image, output.image, desc.width, desc.height);
        ImageBarrier(cmdbuf, output.image, vk::PipelineStageFlagBits2::eTransfer,
                     vk::AccessFlagBits2::eTransferWrite, compute, write);
    }
    Dlss* dlss = Dlss::Get();
    if (!dlss || (create && !dlss->CreateFrameGen(cmdbuf, desc))) {
        Fail(BbSettings::FrameGenDlss, "DLSS Frame Generation could not be created");
        std::scoped_lock lock{targets_mutex};
        for (auto& target : targets) target.ready = false;
        return;
    }
    bool ok = dlss->HasFrameGen(desc, true) || dlss->CreateFrameGen(cmdbuf, desc, true);
    if (ok && !frame.reset) {
        RecordCascadeGuides(cmdbuf, frame, mid, half);
    }
    if (ok) {
        ok = dlss->EvaluateFrameGen(cmdbuf, frame);
    }
    if (ok && !frame.reset) {
        // The primary feature interpolates the two real frames. The secondary advances
        // from the previous real frame to that midpoint, then to the current real frame.
        ImageBarrier(cmdbuf, outputs[1].image, compute, write, compute, read);
        ok = dlss->EvaluateFrameGen(cmdbuf, mid, true);
    }
    if (ok) {
        // The same secondary handle reads history written by the quarter-frame call.
        const vk::MemoryBarrier2 history{
            .srcStageMask = compute, .srcAccessMask = write,
            .dstStageMask = compute, .dstAccessMask = read | write,
        };
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &history});
        ok = dlss->EvaluateFrameGen(cmdbuf, half, true);
    }
    if (!ok) {
        FailCascade("DLSS cascade evaluation failed (see the log)");
        std::scoped_lock lock{targets_mutex};
        for (auto& target : targets) target.ready = false;
    } else if (hudless.image && !frame.reset) {
        for (const auto& output : std::span(outputs).first(CascadeGenerated)) {
            ImageBarrier(cmdbuf, output.image, compute, read | write, compute, write);
        }
        RecordDlssHud(cmdbuf, color, hudless, std::span(outputs).first(CascadeGenerated));
    }
    for (const auto& output : std::span(outputs).first(CascadeGenerated)) {
        ImageBarrier(cmdbuf, output.image, compute, write, all,
                     vk::AccessFlagBits2::eMemoryRead);
    }
    for (const auto& input : {color, hudless, frame.depth, frame.motion}) {
        if (input.image) {
            ImageBarrier(cmdbuf, input.image, compute, read, all, memory,
                         vk::ImageLayout::eGeneral, {input.aspect, 0, 1, 0, 1});
        }
    }
}

#ifdef BB_NATIVE_DLSSG
void FrameGeneration::RecordDirect(const Dlss::FrameGenFrame& frame,
                                   std::span<const Dlss::Resource> outputs) {
    const auto discard = [this] {
        std::scoped_lock lock{targets_mutex};
        for (auto& target : targets) target.ready = false;
    };
    const auto& desc = *dlss_desc;
    scheduler.EndRendering();
    const auto cmd = scheduler.CommandBuffer();
    std::string error;
    auto failed = [&] {
        std::printf("DLSS-G direct: %s. Disabling direct multipliers for this session.\n", error.c_str());
        direct_enabled = false;
        BbSettings::Get().dlssg_max_multiplier = dlss_max_count + 1;
        discard();
    };
    if (!direct_core) {
        const auto start = std::chrono::steady_clock::now();
        direct_core = std::make_unique<player_core::Core>();
        const auto& profile = player_core::profile_1080p_quality();
        if (!direct_core->initialize(instance.GetAllocator(), profile,
                                     direct_model, error)) {
            failed();
            return;
        }
        direct_core->record_initialize(cmd);
        direct_upload_tick = scheduler.CurrentTick();
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
        BbTimeline::Note(BbTimeline::DirectFgInit, outputs.size(),
                         std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        std::printf("DLSS-G direct: native %ux%u from %ux%u, one pair preparation, %zu phases, CPU init %.3f ms\n",
                    desc.width, desc.height, desc.render_width, desc.render_height, outputs.size(), elapsed.count());
    } else if (direct_upload_tick && scheduler.IsFree(direct_upload_tick)) {
        direct_core->release_upload();
        direct_upload_tick = 0;
    }

    const auto image = [](const Dlss::Resource& r) -> player_core::Image {
        return {r.image, r.view, {VkImageAspectFlags(r.aspect), 0, 1, 0, 1},
                VkFormat(r.format), r.width, r.height};
    };
    const bool hudless = bool(frame.hudless.image);
    const bool source_changed = hudless != last_direct_hudless;
    const player_core::Frame source{
        .color = image(hudless ? frame.hudless : frame.backbuffer),
        .depth = image(frame.depth), .motion = image(frame.motion),
        .camera = {.depth_inverted = frame.depth_inverted,
                   .has_matrices = frame.has_matrices,
                   .clip_to_prev_clip = frame.clip_to_prev_clip,
                   .prev_clip_to_clip = frame.prev_clip_to_clip},
        .reset = frame.reset || source_changed,
    };
    if (source_changed) BbTimeline::Note(BbTimeline::DirectFgSource, hudless, scene_id);
    last_direct_hudless = hudless;
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    const auto read = vk::AccessFlagBits2::eMemoryRead;
    const auto write = vk::AccessFlagBits2::eMemoryWrite;
    for (const auto& input : {frame.backbuffer, frame.hudless, frame.depth, frame.motion}) {
        if (input.image) ImageBarrier(cmd, input.image, all, write, all, read,
                                      vk::ImageLayout::eGeneral, {input.aspect, 0, 1, 0, 1});
    }
    if (source.reset) outputs = {};
    std::array<player_core::Image, MaxGenerated> targets_out{};
    for (size_t i = 0; i < outputs.size(); ++i) {
        targets_out[i] = image(outputs[i]);
        ImageBarrier(cmd, outputs[i].image, all, read | write, all, write, vk::ImageLayout::eUndefined);
    }

    const auto result = direct_core->record(cmd, source, std::span(targets_out).first(outputs.size()), error);
    if (result == player_core::RecordResult::failed) {
        failed();
        return;
    }
    if (result == player_core::RecordResult::seeded) {
        discard();
        return;
    }
    if (frame.hudless.image) {
        EnsureDlssOverlayPipelines(false);
        for (const auto& output : outputs) ImageBarrier(cmd, output.image, all, write, all, write);
        RecordDlssHud(cmd, frame.backbuffer, frame.hudless, outputs);
    }
    for (const auto& output : outputs) ImageBarrier(cmd, output.image, all, write, all, read);
}
#endif

void FrameGeneration::RecordDlss(vk::Image color, bool reset, u32 count, bool cascade,
                                 const CameraMatrices::Pose& midpoint,
                                 const std::array<float, 2>& midpoint_jitter,
                                 const CameraMatrices::Pose& previous_pose) {
    const Dlss::FrameGenDesc desc = *dlss_desc;
    const bool create = dlss_create_pending;
    dlss_create_pending = false;
    const vk::ImageView color_view_handle = ColorView(color, scene.color_format);
    // BB_DLSSG_HUDLESS=0: no hudless frame (diagnostics, as toggle bit 30).
    static const bool hudless_wanted = !bb_env_off("BB_DLSSG_HUDLESS");
    const bool with_hudless =
        hudless_wanted && hudless_ready && In().hudless && !BbToggle::Disabled(1u << 30);
    hudless_ready = false;
    const auto resource = [](vk::Image image, vk::ImageView view, vk::Format format,
                             vk::ImageAspectFlags aspect, u32 w, u32 h) {
        return Dlss::Resource{image, view, format, aspect, w, h};
    };
    Dlss::FrameGenFrame frame{
        .backbuffer = resource(color, color_view_handle, scene.color_format,
                               vk::ImageAspectFlagBits::eColor, scene.width, scene.height),
        .depth = resource(vk::Image(In().depth_copy), *In().depth_copy_view, scene.depth_format,
                          vk::ImageAspectFlagBits::eDepth, scene.render_width, scene.render_height),
        .motion = resource(scene.motion, scene.motion_view, vk::Format::eR16G16Sfloat,
                           vk::ImageAspectFlagBits::eColor, scene.render_width, scene.render_height),
        .output = {},
        .hudless = with_hudless ? resource(vk::Image(In().hudless), *In().hudless_view, scene.color_format,
                                           vk::ImageAspectFlagBits::eColor, scene.width, scene.height)
                                : Dlss::Resource{},
        .jitter_x = scene.jitter_x,
        .jitter_y = scene.jitter_y,
        .camera_near = scene.camera_near,
        .camera_far = scene.camera_far,
        .camera_fov = scene.vertical_fov,
        .camera_position = {scene.camera_position[0], scene.camera_position[1], scene.camera_position[2]},
        .camera_up = {scene.camera_up[0], scene.camera_up[1], scene.camera_up[2]},
        .camera_right = {scene.camera_right[0], scene.camera_right[1], scene.camera_right[2]},
        .camera_forward = {scene.camera_forward[0], scene.camera_forward[1], scene.camera_forward[2]},
        .depth_inverted = false, // the game's depth is 0 at the near plane (CameraMotion::Near)
        .has_matrices = scene.has_matrices,
        .view_to_clip = scene.view_to_clip,
        .clip_to_prev_clip = scene.clip_to_prev_clip,
        .prev_clip_to_clip = scene.prev_clip_to_clip,
        .reset = reset,
        .count = count,
        .index = 1,
    };
    std::array<Dlss::Resource, MaxGenerated> outputs{};
    for (u32 i = 0; i < count; ++i) {
        outputs[i] = resource(vk::Image(targets[i].image), *targets[i].view, scene.color_format,
                              vk::ImageAspectFlagBits::eColor, scene.width, scene.height);
    }
#ifdef BB_NATIVE_DLSSG
    if (using_direct) {
        RecordDirect(frame, std::span(outputs).first(count));
        return;
    }
#endif
    if (cascade) {
        RecordDlssCascade(desc, create, frame, outputs, midpoint, midpoint_jitter, previous_pose);
        return;
    }
    const vk::Image hudless_image = with_hudless ? vk::Image(In().hudless) : vk::Image{};

    scheduler.EndRendering();
    auto record = [this, desc, create, frame, outputs, count, color, hudless_image,
                      depth = vk::Image(In().depth_copy), motion = scene.motion, w = scene.width,
                      h = scene.height](vk::CommandBuffer cmdbuf) mutable {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const auto read = vk::AccessFlagBits2::eShaderRead;
        const auto compute = vk::PipelineStageFlagBits2::eComputeShader;
        // The UI drew into the frame; the display pass reads it after. The depth copy and the
        // motion vectors were written earlier in the stream. All in General layout, as NGX
        // takes them.
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite, compute, read);
        if (hudless_image) {
            ImageBarrier(cmdbuf, hudless_image, all, vk::AccessFlagBits2::eMemoryWrite, compute, read);
        }
        ImageBarrier(cmdbuf, depth, all, vk::AccessFlagBits2::eMemoryWrite, compute, read,
                     vk::ImageLayout::eGeneral, DepthRange);
        ImageBarrier(cmdbuf, motion, all, vk::AccessFlagBits2::eMemoryWrite, compute, read);
        // The generated frames start as the real one: DLSS-G leaves its output untouched when it
        // skips a frame (a scene change it detected, its own pacing), and an output entered as
        // Undefined then went to the screen as garbage: black frames flickering in while the
        // camera faced the level, none while it faced the sky (RTX 5060, Windows, 2026-10-27).
        // BB_DLSSG_PREFILL=0: as before.
        static const bool prefill = !bb_env_off("BB_DLSSG_PREFILL");
        for (u32 i = 0; i < count; ++i) {
            if (prefill) {
                CopyFrame(cmdbuf, color, outputs[i].image, w, h);
                ImageBarrier(cmdbuf, outputs[i].image, all, vk::AccessFlagBits2::eMemoryRead,
                             compute, vk::AccessFlagBits2::eShaderStorageWrite);
            } else {
                ImageBarrier(cmdbuf, outputs[i].image, all, vk::AccessFlagBits2::eMemoryRead,
                             compute, vk::AccessFlagBits2::eShaderStorageWrite,
                             vk::ImageLayout::eUndefined);
            }
        }
        Dlss* dlss = Dlss::Get();
        bool ok = dlss != nullptr;
        if (ok && create) {
            // The previous feature was released by Destroy (the GPU done with it).
            ok = dlss->CreateFrameGen(cmdbuf, desc);
            if (!ok) {
                Fail(BbSettings::FrameGenDlss, "DLSS Frame Generation could not be created");
            }
        }
        for (u32 index = 1; ok && index <= count; ++index) {
            frame.output = outputs[index - 1];
            frame.index = index;
            ok = dlss->EvaluateFrameGen(cmdbuf, frame);
            if (!ok) {
                Fail(BbSettings::FrameGenDlss, "DLSS Frame Generation failed (see the log)");
            }
        }
        ImageBarrier(cmdbuf, color, compute, read, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        if (hudless_image) {
            ImageBarrier(cmdbuf, hudless_image, compute, read, all,
                         vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        }
        ImageBarrier(cmdbuf, depth, compute, read, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                     vk::ImageLayout::eGeneral, DepthRange);
        ImageBarrier(cmdbuf, motion, compute, read, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        for (u32 i = 0; i < count; ++i) {
            ImageBarrier(cmdbuf, outputs[i].image, compute, vk::AccessFlagBits2::eShaderStorageWrite,
                         all, vk::AccessFlagBits2::eMemoryRead);
            if (!ok) {
                // Handed out as ready already: the real frame again, not garbage.
                CopyFrame(cmdbuf, color, outputs[i].image, w, h);
            }
        }
    };
    if (cascade_supported) {
        record(scheduler.CommandBuffer());
    } else {
        scheduler.Record(std::move(record));
    }
}

u32 FrameGeneration::TakeOutputs(VAddr display, std::array<Output, MaxGenerated>& out) {
    const int provider = Provider();
    if (provider == BbSettings::FrameGenOff || provider != last_provider) {
        return 0;
    }
    std::scoped_lock lock{targets_mutex};
    if (!ready_display || ready_display != display) {
        ++stat_missed;
        return 0;
    }
    ready_display = 0;
    if (async && provider == BbSettings::FrameGenFsr3) {
        auto& target = async_targets[ready_async_slot];
        target.handed = target.frame;
        out[0] = {vk::Image(target.image), *target.view, target.format, target.width, target.height, target.frame};
        out[0].wait_semaphore = *compute_semaphore;
        out[0].wait_value = target.compute_value;
        out[0].target = ready_async_slot;
        std::scoped_lock lut_lock{lut_mutex};
        if (lut_frame == BbStats::frame_number.load(std::memory_order_relaxed)) {
            out[0].lut_view = lut_view;
            out[0].lut_sampler = lut_sampler;
            out[0].lut_layout = lut_layout;
        }
        ++stat_taken;
        return 1;
    }
    u32 count = 0;
    for (u32 i = 0; i < MaxGenerated; ++i) {
        Target& t = targets[i];
        if (!t.ready || t.frame != targets[0].frame) {
            break;
        }
        out[count] = {vk::Image(t.image), *t.view, t.format, t.width, t.height, t.frame};
        out[count++].disable_interpolation = t.disable_interpolation;
        t.ready = false;
    }
    if (count == 0) {
        ++stat_missed;
    } else {
        ++stat_taken;
    }
    return count;
}

void FrameGeneration::Fail(int provider, const char* reason) {
    auto& settings = BbSettings::Get();
    std::printf("Frame generation: %s; %s off\n", reason, ProviderName(provider));
    if (provider == BbSettings::FrameGenDlss) {
        dlss_failed = true;
        settings.dlssg_problem = reason;
    } else if (provider == BbSettings::FrameGenFsrAmd) {
        amd_failed = true;
        settings.fg_amd_problem = reason;
    } else {
        fsr_failed = true;
        settings.frame_generation_problem = reason;
    }
}

} // namespace Vulkan

namespace Vulkan {

void FrameGeneration::NoteDisplayLut(vk::ImageView view, vk::Sampler sampler,
                                     vk::ImageLayout layout) {
    std::scoped_lock lock{lut_mutex};
    lut_view = view;
    lut_sampler = sampler;
    if (layout != lut_layout || lut_frame == ~0ull) {
        std::printf("Frame generation: the display LUT is sampled in layout %s\n",
                    vk::to_string(layout).c_str());
    }
    lut_layout = layout;
    lut_frame = BbStats::frame_number.load(std::memory_order_relaxed);
}

void FrameGeneration::ApplyDisplayLut(const Output& output) {
    // BB_FG_LUT=0 (diagnostics): the generated frames as FSR made them.
    static const bool off = bb_env_off("BB_FG_LUT");
    if (off) {
        return;
    }
    vk::ImageView view{};
    vk::Sampler sampler{};
    vk::ImageLayout layout{};
    {
        std::scoped_lock lock{lut_mutex};
        // Only this frame's: a view noted frames ago may belong to an image freed since.
        if (lut_frame != BbStats::frame_number.load(std::memory_order_relaxed)) {
            return;
        }
        view = lut_view;
        sampler = lut_sampler;
        layout = lut_layout;
    }
    if (!view || !sampler || !output.view) {
        return;
    }
    if (!EnsureLutPipeline()) {
        return;
    }
    scheduler.Record([this, image = output.image, target = output.view, view, sampler, layout,
                      w = output.width, h = output.height](vk::CommandBuffer cmdbuf) {
        RecordLut(cmdbuf, image, target, w, h, view, sampler, layout);
    });
}

void FrameGeneration::RecordDisplayLut(vk::CommandBuffer cmdbuf, const Output& output) {
    static const bool off = bb_env_off("BB_FG_LUT");
    if (off || !output.lut_view || !output.lut_sampler || !output.view || !EnsureLutPipeline()) {
        return;
    }
    RecordLut(cmdbuf, output.image, output.view, output.width, output.height, output.lut_view,
              output.lut_sampler, output.lut_layout);
}

bool FrameGeneration::EnsureLutPipeline() {
    static std::mutex mutex; // the draw stream (ApplyDisplayLut) or the presenter (asynchronous)
    std::scoped_lock lock{mutex};
    if (lut_pipeline) {
        return true;
    }
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {.binding = 0, .descriptorType = vk::DescriptorType::eStorageImage,
         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 2, .descriptorType = vk::DescriptorType::eSampler,
         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute},
    }};
    lut_desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()), .pBindings = bindings.data()}));
    lut_pipeline_layout = Check(device.createPipelineLayoutUnique(
        {.setLayoutCount = 1, .pSetLayouts = &*lut_desc_layout}));
    const auto module = CompileSPV(FG_DISPLAY_LUT_COMP, device);
    lut_pipeline = Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
                .layout = *lut_pipeline_layout}));
    device.destroyShaderModule(module);
    std::printf("Frame generation: generated frames get the game's display LUT\n");
    return bool(lut_pipeline);
}

void FrameGeneration::RecordLut(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageView target,
                                u32 w, u32 h, vk::ImageView view, vk::Sampler sampler,
                                vk::ImageLayout layout) {
    const vk::ImageMemoryBarrier2 before{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
    const vk::DescriptorImageInfo frame_info{.imageView = target, .imageLayout = vk::ImageLayout::eGeneral};
    // In the layout the game's display pass sampled it in (its descriptor's, noted with it):
    // read-only optimal was assumed, while the texture cache keeps many images General; AMD reads
    // either, NVIDIA read a black LUT, and every generated frame came out black (DLSS FG,
    // RTX 5060, Windows, 2026-10-27).
    const vk::DescriptorImageInfo lut_info{.imageView = view, .imageLayout = layout};
    const vk::DescriptorImageInfo sampler_info{.sampler = sampler};
    const std::array<vk::WriteDescriptorSet, 3> writes{{
        {.dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &frame_info},
        {.dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &lut_info},
        {.dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampler,
         .pImageInfo = &sampler_info},
    }};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *lut_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *lut_pipeline_layout, 0, writes);
    cmdbuf.dispatch((w + 7) / 8, (h + 7) / 8, 1);
    const vk::ImageMemoryBarrier2 after{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
}

} // namespace Vulkan
