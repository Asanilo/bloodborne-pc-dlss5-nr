// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/player_diagnostics.h"
#include "render/presenter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <limits>

#include <vk_mem_alloc.h>

#include "bbport_game_menu.h"
#include "bbport_overlay.h"
#include "render/nr_runtime_control.h"
#include "render/nr_runtime_capture.h"
#include "render/sr_timing_math.h"
#include <cstring>
#include "bbport_timeline.h"
#include "bbport_toggles.h"
#include "core/emulator_settings.h"
#include "buffers/memory_model.h"
#include "sdl_window.h"
#include "textures/image.h"
#include "textures/texture_cache.h"
#include "upscale/vk_frame_generation.h"
#include "upscale/frame_generation_lifecycle.h"
#include "upscale/frame_generation_contract.h"
#include "upscale/vk_temporal_upscaler.h"
#include "render/rasterizer.h"
#include "vulkan/platform.h"

namespace Vulkan {

namespace {

constexpr vk::ImageSubresourceRange ColorRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

/// How the display buffer is read: its format as a view (the 10-bit sRGB one has no sRGB
/// Vulkan format: it is decoded in the encode pass).
/// The shown frame's aspect (bbgpu_frame_aspect): any thread.
std::atomic<float> shown_aspect{16.0f / 9.0f};

vk::Format FrameViewFormat(BbVideoOut::PixelFormat format) {
    switch (format) {
    case BbVideoOut::PixelFormat::A8B8G8R8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case BbVideoOut::PixelFormat::A8R8G8B8Srgb:
        return vk::Format::eB8G8R8A8Srgb;
    case BbVideoOut::PixelFormat::A2R10G10B10:
    case BbVideoOut::PixelFormat::A2R10G10B10Srgb:
    case BbVideoOut::PixelFormat::A2R10G10B10Bt2020Pq:
        return vk::Format::eA2R10G10B10UnormPack32;
    default:
        UNREACHABLE_MSG("display buffer format {:#x}", u32(format));
    }
}

/// `frame` fitted into `target` keeping its aspect, centered.
vk::Rect2D Letterbox(vk::Extent2D frame, vk::Extent2D target) {
    // A frame prepared before a resize may still have the old aspect. Its destination must
    // stay nonempty too, even before SetExpectedSize reaches the next prepared frame.
    const auto fitted = PresentSizing::FitFrame({frame.width, frame.height},
                                               {target.width, target.height});
    const vk::Extent2D size{fitted.width, fitted.height};
    return {{s32(target.width - size.width) / 2, s32(target.height - size.height) / 2}, size};
}

/// BB_FRAMES_AHEAD (default 1): how many guest frames the command processor thread may run
/// ahead of the GPU (0: unbounded).
u32 FramesAhead() {
    static const u32 value = [] {
        const char* env = std::getenv("BB_FRAMES_AHEAD");
        return env ? u32(std::max(0, std::atoi(env))) : 1u;
    }();
    return value;
}

void WaitFence(vk::Device device, vk::Fence fence) {
    while (true) {
        const vk::Result result = device.waitForFences(fence, true, std::numeric_limits<u64>::max());
        if (result == vk::Result::eSuccess) {
            return;
        }
        if (result != vk::Result::eTimeout) {
            Check<"wait for a frame">(result);
        }
    }
}

} // namespace

Presenter::Presenter(Frontend::WindowSDL& window_, BbCp::CommandProcessor* cp_)
    : window{window_},
      instance{window, EmulatorSettings.GetGpuId(), EmulatorSettings.IsVkValidationEnabled(),
               EmulatorSettings.IsVkCrashDiagnosticEnabled()},
      cp{cp_}, draw_scheduler{instance, true}, present_scheduler{instance, false, instance.GetPresentQueue()},
      swapchain{instance, window}, runtime{instance, draw_scheduler},
      rasterizer{std::make_unique<Rasterizer>(instance, draw_scheduler, runtime, cp)},
      texture_cache{rasterizer->GetTextureCache()},
      encode_pass{instance.GetDevice(), swapchain.GetSurfaceFormat().format} {
    const vk::Device device = instance.GetDevice();
    const u32 count = swapchain.GetImageCount();
    frames.resize(count);
    for (u32 i = 0; i < count; ++i) {
        frames[i].id = u8(i);
        frames[i].present_done = Check<"create a frame fence">(
            device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
        free_frames.push_back(&frames[i]);
    }
    BbOverlay::Init(instance, swapchain.GetSurfaceFormat().format, count);
}

Presenter::~Presenter() {
    draw_scheduler.Finish();
    present_scheduler.Finish();
    const vk::Device device = instance.GetDevice();
    for (auto& frame : frames) {
        for(auto& output:frame.interp){device.destroyImageView(output.view);if(output.image)vmaDestroyImage(instance.GetAllocator(),output.image,output.allocation);output={};}
        device.destroyImageView(frame.image_view);
        if (frame.image) {
            vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
        }
        device.destroyFence(frame.present_done);
    }
}

void Presenter::SetHDR(bool enable) {
    if (!IsHDRSupported()) {
        return;
    }
    if (swapchain.GetHDR() != enable) {
        // Changing HDR recreates the swapchain and waits for the device: exclude queue submits.
        std::scoped_lock lock{Scheduler::submit_mutex};
        swapchain.SetHDR(enable);
    }
    hdr.store(enable, std::memory_order_relaxed);
}

VideoCore::Image& Presenter::RegisterVideoOutSurface(
    const BbVideoOut::BufferAttributeGroup& attribute, VAddr cpu_address) {
    auto desc = VideoCore::TextureCache::ImageDesc{attribute, cpu_address};
    const auto image_id = texture_cache.FindImage(desc);
    auto& image = texture_cache.GetImage(image_id);
    image.usage.vo_surface = 1u;
    return image;
}

void Presenter::RecreateFrame(Frame& frame, u32 width, u32 height) {
    std::scoped_lock fg_lock{fg_images_mutex};
    const vk::Device device = instance.GetDevice();
    device.destroyImageView(frame.image_view);
    if (frame.image) {
        vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
    }
    const vk::Format format = swapchain.GetSurfaceFormat().format;
    const VkImageCreateInfo image_info = vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
    };
    const VmaAllocationCreateInfo alloc_info{
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    VkImage image{};
    const VkResult result = vmaCreateImage(instance.GetAllocator(), &image_info, &alloc_info,
                                           &image, &frame.allocation, nullptr);
    Check<"allocate a frame image">(vk::Result(result));
    frame.image = image;
    frame.image_format = format;
    frame.image_view = Check<"create a frame view">(device.createImageView({
        .image = frame.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = ColorRange,
    }));
    SetObjectName(device, frame.image, "frame {}", frame.id);
    // Frame generation's frame (made with the first one generated: most sessions never need it).
    for(auto& output:frame.interp){device.destroyImageView(output.view);if(output.image)vmaDestroyImage(instance.GetAllocator(),output.image,output.allocation);output={};}
    frame.has_interp = false;
    frame.width = width;
    frame.height = height;
    frame.is_hdr = swapchain.GetHDR();
}

Frame* Presenter::GetRenderFrame() {
    const auto begin = std::chrono::steady_clock::now();
    Frame* frame;
    {
        std::unique_lock lock{free_mutex};
        free_cv.wait(lock, [this] { return !free_frames.empty(); });
        frame = free_frames.front();
        free_frames.pop_front();
    }
    const auto acquired = std::chrono::steady_clock::now();
    // Its last present has finished: the image, its FSR slot and the fence are free.
    WaitFence(instance.GetDevice(), frame->present_done);
    const auto fenced = std::chrono::steady_clock::now();
    if (frame->present_tick) {
        const vk::Semaphore semaphore = present_scheduler.GetWorkSemaphore()->Handle();
        const vk::SemaphoreWaitInfo wait{
            .semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &frame->present_tick,
        };
        Check<"wait for all frame readers">(
            instance.GetDevice().waitSemaphores(wait, std::numeric_limits<u64>::max()));
    }
    const auto source = BbStats::gpu_frames.load(std::memory_order_relaxed);
    if (std::getenv("BB_FG_PROFILE") && source && source % 31 == 0) {
        auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        std::printf("FG_FRAME_ACQUIRE_PROFILE source=%llu free_pool_cpu_ms=%.3f old_fence_cpu_ms=%.3f old_present_tick_cpu_ms=%.3f\n",
            (unsigned long long)source,ms(begin,acquired),ms(acquired,fenced),ms(fenced,std::chrono::steady_clock::now()));
    }
    const auto [width, height] =
        PresentSizing::Unpack(expected_extent.load(std::memory_order_relaxed));
    if (frame->width != width || frame->height != height ||
        frame->is_hdr != swapchain.GetHDR()) {
        RecreateFrame(*frame, width, height);
    }
    return frame;
}

void Presenter::ReleaseFrame(Frame* frame) {
    if (frame == &blank_frame) {
        return;
    }
    {
        std::scoped_lock lock{free_mutex};
        free_frames.push_back(frame);
    }
    free_cv.notify_one();
}

Frame* Presenter::PrepareFrame(const BbVideoOut::BufferAttributeGroup& attribute,
                               VAddr cpu_address) {
    // Normal exit retired the swapchain: this thread must not prepare another frame for it.
    // Guarded by the same mutex the retire waits on, so no lease can be taken after the gate.
    {
        std::scoped_lock lock{prepare_gate_mutex};
        if (prepare_stopped.load(std::memory_order_acquire)) {
            return &blank_frame;
        }
        ++prepare_in_flight;
    }
    struct PrepareLease {
        std::mutex& mutex;
        unsigned& count;
        ~PrepareLease() {
            std::scoped_lock lock{mutex};
            --count;
        }
    } prepare_lease{prepare_gate_mutex, prepare_in_flight};
    BbTimeline::Note(BbTimeline::PipeTask, 4, draw_scheduler.CurrentTick());
    // Scaled upscaler presets: the output-size display buffer drawn by the port.
    TemporalUpscaler::Display display{};
    auto& upscaler = rasterizer->GetUpscaler();
    const bool upscaled = upscaler.DisplayOverride(cpu_address, display);
    u64 native_fg_source=0;
    C1PresentAudit c1_audit{};
    VideoCore::ImageId image_id{};
    if (!upscaled) {
        auto desc = VideoCore::TextureCache::ImageDesc{attribute, cpu_address};
        image_id = texture_cache.FindImage(desc);
        texture_cache.UpdateImage(image_id);
    }

    Frame* frame = GetRenderFrame();
    frame->prepare_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Recorded like draws, on the recording threads: CommandBuffer() would first wait for them
    // to record everything handed over before and then submit from this thread.
    draw_scheduler.EndRendering();

    VideoCore::ImageViewInfo view_info{};
    view_info.format = FrameViewFormat(attribute.attrib.pixel_format);
    // The frame is opaque: alpha would blend it with the overlay.
    view_info.mapping.a = vk::ComponentSwizzle::eOne;
    vk::ImageView source{};
    vk::Extent2D source_size{};
    if (upscaled) {
        const vk::Device device = instance.GetDevice();
        const vk::ImageViewUsageCreateInfo usage{.usage = vk::ImageUsageFlagBits::eSampled};
        source = Check(device.createImageView({
            .pNext = &usage,
            .image = display.image,
            .viewType = vk::ImageViewType::e2D,
            .format = view_info.format,
            .components = view_info.mapping,
            .subresourceRange = ColorRange,
        }));
        draw_scheduler.DeferOperation([device, source] { device.destroyImageView(source); });
        source_size = vk::Extent2D{display.width, display.height};
        draw_scheduler.Record([image = display.image](vk::CommandBuffer cmdbuf) {
            const vk::ImageMemoryBarrier2 to_read{
                .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = image,
                .subresourceRange = ColorRange,
            };
            cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_read});
        });
    } else {
        auto& image = texture_cache.GetImage(image_id);
        source = *image.FindView(view_info).image_view;
        source_size = vk::Extent2D{image.info.size.width, image.info.size.height};
        const u64 frame_counter_before = BbStats::frame_number.load(std::memory_order_relaxed);
        const u64 gpu_frame_at_prepare = BbStats::gpu_frames.load(std::memory_order_relaxed);
        const u64 prepare_tick = draw_scheduler.CurrentTick();
        native_fg_source=upscaler.TakeNativeFgDisplaySource(cpu_address,image.image_uid,
            image.info.size.width,image.info.size.height,gpu_frame_at_prepare,prepare_tick);
        const bool c1_selected = upscaler.TakeC1PresentAudit(
            cpu_address, frame_counter_before, gpu_frame_at_prepare, prepare_tick, c1_audit);
        // The 1:1 HDR Run route consumes the actual guest VideoOut buffer, not the scaled
        // display override. Bind this one-shot ledger to that real address and buffer.
        if (c1_selected && ((c1_audit.stage != "C3" && c1_audit.stage != "C4" && c1_audit.stage != "P0" && c1_audit.stage != "NATIVE_ASYNC" && c1_audit.stage != "NATIVE_ASYNC_PRE_SR") || c1_audit.audit_full)) {
            runtime.Transit(&image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eTransfer,
                            vk::AccessFlagBits2::eTransferRead);
            runtime.FlushBarriers();
            upscaler.DumpPresented(image.GetImage(), source_size.width, source_size.height,
                                   view_info.format);
        }
        runtime.Transit(&image, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::PipelineStageFlagBits2::eFragmentShader |
                            vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderRead);
        runtime.FlushBarriers();
    }
    const C1PresentAudit& frame_audit = upscaled ? display.c1_audit : c1_audit;
    frame->c1_source_game_frame = frame_audit.source_game_frame;
    // Both display routes carry their actual recorded content stamp. NR can be
    // disabled or unarmed without removing the FG source or advancing NR history.
    frame->fg_real_source = upscaled ? display.source_game_frame : native_fg_source;
    frame->c1_merge_tick = frame_audit.merge_tick;
    frame->c1_guest_buffer_address = frame_audit.buffer_address;
    frame->c1_guest_prepare_gpu_frame = frame_audit.buffer_prepare_gpu_frame;
    frame->c1_guest_prepare_tick = frame_audit.buffer_prepare_tick;
    frame->c1_present_frame_number = 0;
    frame->audit_stage = frame_audit.stage;
    frame->audit_execution_mode = frame_audit.execution_mode;
    frame->audit_mode = frame_audit.mode;
    frame->audit_session = frame_audit.session;
    frame->audit_selected_output = frame_audit.selected_output;
    frame->p0_selection_known = true;
    frame->audit_nr_frame = frame_audit.nr_frame;
    frame->audit_full = frame_audit.audit_full;
    frame->runtime_capture=frame_audit.runtime_capture;
    expected_ratio.store(float(source_size.width) / float(source_size.height),
                         std::memory_order_relaxed);
    shown_aspect.store(float(source_size.width) / float(source_size.height),
                       std::memory_order_relaxed);

    const EncodePass::Settings encode{
        .gamma = gamma.load(std::memory_order_relaxed),
        .hdr = hdr.load(std::memory_order_relaxed),
        .srgb_input = attribute.attrib.pixel_format == BbVideoOut::PixelFormat::A2R10G10B10Srgb,
    };
    // Numbered here, in command order; marked where the commands are recorded.
    const u32 stream = draw_scheduler.CrumbStream();
    const u32 crumb = Breadcrumbs::Enabled()
                          ? Breadcrumbs::Note(stream, {.name = "presenter encode"})
                          : 0;
    // The encode pass holds no state: frames of different submissions may be recorded on two
    // threads at once.
    draw_scheduler.Record([this, frame, source, encode, stream, crumb](vk::CommandBuffer cmdbuf) {
        if (crumb) {
            Breadcrumbs::Mark(cmdbuf, stream, crumb, false);
        }
        const vk::Extent2D size{frame->width, frame->height};
        // The frame's last reader (the present blit) is done: the fence was waited for.
        const vk::ImageMemoryBarrier2 to_attachment{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .image = frame->image,
            .subresourceRange = ColorRange,
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_attachment});
        encode_pass.Render(cmdbuf, source, frame->image_view, size, encode);
        // Read by the present blit (or a dump) in the general layout, after a semaphore wait.
        const vk::ImageMemoryBarrier2 to_general{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = frame->image,
            .subresourceRange = ColorRange,
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_general});
        if (crumb) {
            Breadcrumbs::Mark(cmdbuf, stream, crumb, true);
        }
    });

    // Frame generation: the frame interpolated before this one, encoded the same way.
    frame->has_interp = false;
    frame->interp_count=1;frame->generated_index=0;frame->fg_epoch=0;
    frame->interp_recorded.reset();
    frame->interp_eligible.reset();
    frame->fg_capture_cpu_ns = 0;
    frame->fg_guide_record_cpu_ns = frame->fg_producer_submit_cpu_ns = frame->flip_queue_cpu_ns = 0;
    frame->fg_frame = 0;
    frame->fg_strict_source = false;
    frame->fg_previous_source = 0;
    frame->fg_ticket.reset();
    if (auto* fg = upscaler.GetFrameGeneration()) {
        auto ticket = fg->TakeAsyncOutput(cpu_address);
        if (ticket && ticket->raw->width == source_size.width && ticket->raw->height == source_size.height &&
            FgContract::SameRealSource(ticket->source, frame->fg_real_source)) {
            frame->fg_ticket = std::move(ticket);
            frame->fg_encode = encode; frame->fg_view_format = view_info.format;
            frame->fg_strict_source = true; frame->fg_previous_source = frame->fg_ticket->previous_source;
            frame->fg_capture_cpu_ns = frame->fg_ticket->capture_cpu_ns;
            frame->fg_frame = frame->fg_ticket->source;
            frame->fg_guide_record_cpu_ns = frame->fg_ticket->guide_record_cpu_ns;
            frame->fg_producer_submit_cpu_ns = frame->fg_ticket->producer_submit_cpu_ns;
        }
    }
    FrameGeneration::Output interp{};
    if (upscaler.GetFrameGeneration() &&
        upscaler.GetFrameGeneration()->TakeOutput(cpu_address, interp) &&
        interp.width == source_size.width && interp.height == source_size.height &&
        // A separately proved actual display source owns this generated image.
        // Scaled source identity does not depend on NR's optional Present ledger.
        ((!interp.strict_source && upscaled) || FgContract::SameRealSource(interp.source_game_frame,
                                               frame->fg_real_source))) {
        EncodeInterpolated(frame, interp.image, interp.format, interp.width, interp.height,
                           view_info.format, encode, draw_scheduler);
        frame->interp_recorded = interp.recorded;
        frame->interp_eligible = interp.eligible;
        frame->fg_capture_cpu_ns = interp.capture_cpu_ns;
        frame->fg_frame = interp.frame;
        frame->fg_strict_source = interp.strict_source;
        frame->fg_previous_source = interp.previous_source_game_frame;
    }

    frame->c1_present_frame_number = BbStats::frame_number.fetch_add(1, std::memory_order_relaxed) + 1;
    frame->ready_semaphore = draw_scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = draw_scheduler.CurrentTick();
    SubmitInfo info{};
    draw_scheduler.NoteFrameEndSubmission();
    draw_scheduler.Flush(info);
    if (frame->c1_source_game_frame != 0 && frame->audit_stage == "P0") {
        std::printf("P0_PRESENT_PREPARED stage=P0 execution_mode=%s mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx gpu_frame_at_prepare=%llu guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu audit_kind=%s\n",
            frame->audit_execution_mode.c_str(), frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
            frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
            (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
            (unsigned long long)frame->c1_guest_prepare_gpu_frame,
            (unsigned long long)frame->c1_guest_prepare_tick,
            (unsigned long long)frame->ready_tick, (unsigned long long)frame->c1_present_frame_number,
            frame->audit_full ? "full" : "off");
    }
    if (frame->c1_source_game_frame != 0 && frame->audit_stage == "Q1") {
        std::printf("Q1_PRESENT_PREPARED stage=Q1 execution_mode=%s mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx gpu_frame_at_prepare=%llu guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu audit_kind=%s\n",
            frame->audit_execution_mode.c_str(), frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
            frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
            (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
            (unsigned long long)frame->c1_guest_prepare_gpu_frame,
            (unsigned long long)frame->c1_guest_prepare_tick,
            (unsigned long long)frame->ready_tick, (unsigned long long)frame->c1_present_frame_number,
            frame->audit_full ? "full" : "off");
    } else if (frame->c1_source_game_frame != 0 && frame->audit_stage == "C4") {
        std::printf("C4_PRESENT_PREPARED stage=C4 execution_mode=%s mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx gpu_frame_at_prepare=%llu guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu audit_kind=%s\n",
            frame->audit_execution_mode.c_str(), frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
            frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
            (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
            (unsigned long long)frame->c1_guest_prepare_gpu_frame,
            (unsigned long long)frame->c1_guest_prepare_tick,
            (unsigned long long)frame->ready_tick, (unsigned long long)frame->c1_present_frame_number,
            frame->audit_full ? "full" : "off");
    } else if (frame->c1_source_game_frame != 0 && frame->audit_stage == "C3") {
        std::printf("C3_PRESENT_PREPARED stage=C3 mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx gpu_frame_at_prepare=%llu guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu audit_kind=%s\n",
            frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
            frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
            (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
            (unsigned long long)frame->c1_guest_prepare_gpu_frame,
            (unsigned long long)frame->c1_guest_prepare_tick,
            (unsigned long long)frame->ready_tick, (unsigned long long)frame->c1_present_frame_number,
            frame->audit_full ? "full" : "gpu_full_scan");
    } else if (frame->c1_source_game_frame != 0 && frame->audit_stage == "C2") {
        std::printf("C2_PRESENT_PREPARED stage=C2 mode=%s session=%s selected_output=%u "
                    "route=nonupscaled_videoout source_game_frame=%llu merge_tick=%llu "
                    "guest_buffer_address=0x%llx gpu_frame_at_prepare=%llu "
                    "frame_counter_before=%llu present_frame_number=%llu guest_prepare_tick=%llu ready_tick=%llu\n",
                    frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_selected_output,
                    static_cast<unsigned long long>(frame->c1_source_game_frame),
                    static_cast<unsigned long long>(frame->c1_merge_tick),
                    static_cast<unsigned long long>(frame->c1_guest_buffer_address),
                    static_cast<unsigned long long>(frame->c1_guest_prepare_gpu_frame),
                    static_cast<unsigned long long>(c1_audit.present_frame_counter_before),
                    static_cast<unsigned long long>(frame->c1_present_frame_number),
                    static_cast<unsigned long long>(frame->c1_guest_prepare_tick),
                    static_cast<unsigned long long>(frame->ready_tick));
    } else if (VerbosePlayerFrames() && frame->c1_source_game_frame != 0) {
        std::printf("C1_PRESENT_PREPARED route=nonupscaled_videoout source_game_frame=%llu merge_tick=%llu "
                    "guest_buffer_address=0x%llx gpu_frame_at_prepare=%llu "
                    "frame_counter_before=%llu present_frame_number=%llu "
                    "guest_prepare_tick=%llu ready_tick=%llu\n",
                    static_cast<unsigned long long>(frame->c1_source_game_frame),
                    static_cast<unsigned long long>(frame->c1_merge_tick),
                    static_cast<unsigned long long>(frame->c1_guest_buffer_address),
                    static_cast<unsigned long long>(c1_audit.buffer_prepare_gpu_frame),
                    static_cast<unsigned long long>(c1_audit.present_frame_counter_before),
                    static_cast<unsigned long long>(frame->c1_present_frame_number),
                    static_cast<unsigned long long>(frame->c1_guest_prepare_tick),
                    static_cast<unsigned long long>(frame->ready_tick));
    }
    BbTimeline::Note(BbTimeline::PipeTask, 5, frame->ready_tick);

    // The command processor thread runs at most FramesAhead() guest frames ahead of the GPU: it
    // waits here for the frame that many flips back. When the GPU is the bottleneck it finishes
    // frames at an even rate; unbounded, the thread ran ahead and then blocked wherever a
    // resource ran out, so flips (and the guest's frame timing) came in bursts.
    if (const u32 ahead = FramesAhead()) {
        recent_frame_ticks.push_back(frame->ready_tick);
        if (recent_frame_ticks.size() > ahead) {
            const u64 tick = recent_frame_ticks.front();
            recent_frame_ticks.pop_front();
            BbStats::WaitTimer timer{BbStats::present_wait_ns};
            const auto begin = std::chrono::steady_clock::now();
            draw_scheduler.Wait(tick);
            if (std::getenv("BB_FG_PROFILE") && frame->fg_real_source % 31 == 0)
                std::printf("FG_FRAMES_AHEAD_PROFILE source=%llu wait_cpu_ms=%.3f bound=%u\n",
                    (unsigned long long)frame->fg_real_source,
                    std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count(),ahead);
        }
    }
    return frame;
}

Frame* Presenter::PrepareBlankFrame(bool present_thread) {
    // Presented as the black bars alone: nothing to render. A blank flip of the guest still
    // ends its submission, as any flip does: the guest may wait for work recorded before it.
    if (!present_thread) {
        draw_scheduler.EndRendering();
        SubmitInfo info{};
        draw_scheduler.Flush(info);
    }
    return &blank_frame;
}

void Presenter::WaitRendered(const Frame* frame) {
    if (!frame || !frame->ready_semaphore) {
        return;
    }
    const vk::SemaphoreWaitInfo info{
        .semaphoreCount = 1,
        .pSemaphores = &frame->ready_semaphore,
        .pValues = &frame->ready_tick,
    };
    // A lost device ends the wait (the presenter reports it); a timeout waits again.
    while (instance.GetDevice().waitSemaphores(&info, 1'000'000'000) == vk::Result::eTimeout) {
    }
}

void Presenter::SetExpectedSize(u32 width, u32 height) {
    const auto fitted = PresentSizing::Fit(width, height,
                                          expected_ratio.load(std::memory_order_relaxed));
    if (!fitted.Valid()) return; // suspended surface: retain the last valid frame extent
    expected_extent.store(PresentSizing::Pack(fitted), std::memory_order_relaxed);
}

bool Presenter::EncodeInterpolated(Frame* frame, vk::Image image, vk::Format format, u32 width,
                                   u32 height, vk::Format display_view_format,
                                   const EncodePass::Settings& encode, Scheduler& scheduler, unsigned slot) {
    std::scoped_lock fg_lock{fg_images_mutex};
    const vk::Device device = instance.GetDevice();
    if (!frame->interp[slot].image) {
        const VkImageCreateInfo image_info = vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = swapchain.GetSurfaceFormat().format,
            .extent = {frame->width, frame->height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
        };
        const VmaAllocationCreateInfo alloc_info{
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        VkImage created{};
        if (vmaCreateImage(instance.GetAllocator(), &image_info, &alloc_info, &created,
                           &frame->interp[slot].allocation, nullptr) != VK_SUCCESS) {
            return false;
        }
        frame->interp[slot].image = created;
        frame->interp[slot].view = Check<"create a frame view">(device.createImageView({
            .image = frame->interp[slot].image,
            .viewType = vk::ImageViewType::e2D,
            .format = swapchain.GetSurfaceFormat().format,
            .subresourceRange = ColorRange,
        }));
    }
    // Read as the display buffer is, with its view format: the game's display pass copies the
    // scene image's bytes as they are into a BGRA buffer, so the scene image (R8G8B8A8 by its
    // format) holds them in that order too; its own format swapped red and blue. 8-bit images are
    // made mutable (vk_frame_generation), 32-bit formats view each other.
    const bool rgba8 = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eB8G8R8A8Unorm;
    const bool display8 = display_view_format == vk::Format::eB8G8R8A8Srgb ||
                          display_view_format == vk::Format::eR8G8B8A8Srgb;
    const vk::Format view_format = rgba8 && display8 ? display_view_format : format;
    vk::ComponentMapping mapping{};
    mapping.a = vk::ComponentSwizzle::eOne;
    const vk::ImageViewUsageCreateInfo usage{.usage = vk::ImageUsageFlagBits::eSampled};
    const vk::ImageView source = Check(device.createImageView({
        .pNext = &usage,
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = view_format,
        .components = mapping,
        .subresourceRange = ColorRange,
    }));
    scheduler.DeferOperation([device, source] { device.destroyImageView(source); });
    scheduler.Record([this, frame, image, source, encode, slot](vk::CommandBuffer cmdbuf) {
        const std::array<vk::ImageMemoryBarrier2, 2> before{{
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = image,
                .subresourceRange = ColorRange,
            },
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .image = frame->interp[slot].image,
                .subresourceRange = ColorRange,
            },
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = before.data()});
        encode_pass.Render(cmdbuf, source, frame->interp[slot].view, {frame->width, frame->height}, encode);
        const std::array<vk::ImageMemoryBarrier2, 2> after{{
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = image,
                .subresourceRange = ColorRange,
            },
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = frame->interp[slot].image,
                .subresourceRange = ColorRange,
            },
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = after.data()});
    });
    frame->has_interp = true;
    return true;
}

void Presenter::RecordPresent(vk::CommandBuffer cmdbuf, const Frame* frame, vk::Image target,
                              vk::Extent2D extent, vk::Image source) {
    const vk::Rect2D rect =
        frame ? Letterbox({frame->width, frame->height}, extent) : vk::Rect2D{{0, 0}, {0, 0}};
    const bool bars = rect.extent != extent;
    const vk::ImageMemoryBarrier2 to_transfer{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = target,
        .subresourceRange = ColorRange,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_transfer});
    if (bars) {
        const vk::ClearColorValue black{std::array{0.0f, 0.0f, 0.0f, 1.0f}};
        cmdbuf.clearColorImage(target, vk::ImageLayout::eTransferDstOptimal, black, ColorRange);
    }
    if (frame) {
        if (bars) {
            const vk::MemoryBarrier2 cleared{
                .srcStageMask = vk::PipelineStageFlagBits2::eClear,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eBlit,
                .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            };
            cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &cleared});
        }
        const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        const vk::ImageBlit region{
            .srcSubresource = layers,
            .srcOffsets = std::array{vk::Offset3D{0, 0, 0},
                                     vk::Offset3D{s32(frame->width), s32(frame->height), 1}},
            .dstSubresource = layers,
            .dstOffsets = std::array{vk::Offset3D{rect.offset.x, rect.offset.y, 0},
                                     vk::Offset3D{rect.offset.x + s32(rect.extent.width),
                                                  rect.offset.y + s32(rect.extent.height), 1}},
        };
        cmdbuf.blitImage(source ? source : frame->image, vk::ImageLayout::eGeneral, target,
                         vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eLinear);
    }

    // The port's menu and FPS counter over the frame, at display resolution.
    BbGameMenu::Poll();
    const bool overlay = BbOverlay::Visible();
    const vk::ImageMemoryBarrier2 after_transfer{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = overlay ? vk::PipelineStageFlagBits2::eColorAttachmentOutput
                                : vk::PipelineStageFlagBits2::eNone,
        .dstAccessMask = overlay ? vk::AccessFlagBits2::eColorAttachmentRead |
                                       vk::AccessFlagBits2::eColorAttachmentWrite
                                 : vk::AccessFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = overlay ? vk::ImageLayout::eColorAttachmentOptimal
                             : vk::ImageLayout::ePresentSrcKHR,
        .image = target,
        .subresourceRange = ColorRange,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after_transfer});
    if (overlay) {
        BbOverlay::Render(cmdbuf, swapchain.ImageView(), extent);
        const vk::ImageMemoryBarrier2 to_present{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::ePresentSrcKHR,
            .image = target,
            .subresourceRange = ColorRange,
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_present});
    }
}

void Presenter::Present(Frame* frame, bool is_reusing_frame, bool is_game_frame) {
    bool released = false;
    const auto release = [&] {
        if (!is_reusing_frame && !released) {
            released = true;
            ReleaseFrame(frame);
        }
    };
    const bool blank = frame == &blank_frame;
    if (FgLifecycle::exit_requested.load()) {
        frame->fg_ticket.reset(); // Worker owns the copy/ACK; no unsubmitted reader is retained.
        frame->has_interp = false;
        if (FgLifecycle::draw_retired.load() && !FgLifecycle::present_retired.load()) {
            if (FgLifecycle::driver_smooth_motion.load()) {
                // The command processor thread prepares the next frame on the same Presenter.
                // Refuse that before any swapchain resource changes, then let this frame go so a
                // PrepareFrame already waiting for a free frame can finish, and wait for the
                // leases already taken: destroying views/semaphores under an in-flight
                // PrepareFrame is a use-after-free the device reports as lost.
                {
                    std::scoped_lock lock{prepare_gate_mutex};
                    prepare_stopped.store(true, std::memory_order_release);
                }
                release();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (true) {
                    {
                        std::scoped_lock lock{prepare_gate_mutex};
                        if (!prepare_in_flight) break;
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        std::fprintf(stderr,"SM_EXIT_HOLD functional=FAIL reason=prepare_in_flight_timeout device_idle=UNKNOWN\n");
                        FgLifecycle::held.store(true);
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            // All old FG copies were submitted before draw_retired; this owning thread has
            // submitted its last generated present and will admit no more.
            auto leases_drained = std::make_shared<std::promise<void>>();
            auto drained = leases_drained->get_future();
            present_scheduler.DeferPriorityOperation([leases_drained] { leases_drained->set_value(); });
            present_scheduler.Finish();
            if (drained.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
                FgLifecycle::held.store(true);
                std::fprintf(stderr,"DLSSG_PRESENT_HOLD raw output lease retirement timeout\n");
                std::abort();
            }
            std::scoped_lock fg_lock{fg_images_mutex};
            const auto device = instance.GetDevice();
            for (auto& owned : frames) {
                owned.fg_ticket.reset();
                for(auto& output:owned.interp){device.destroyImageView(output.view);if(output.image)vmaDestroyImage(instance.GetAllocator(),output.image,output.allocation);output={};}
            }
            if (FgLifecycle::driver_smooth_motion.load()) {
                if (!swapchain.RetireDriverLayer()) {
                    std::fprintf(stderr,"SM_EXIT_HOLD functional=FAIL device_idle=UNKNOWN\n");
                    FgLifecycle::held.store(true);
                    release();
                    return;
                }
                std::printf("SM_EXIT_PROOF device_idle_before=1 swapchain_destroy_returned=1 device_idle_after=1 scope=Vulkan_layer_API_contract\n");
            }
            FgLifecycle::present_retired.store(true);
            std::printf("FG_PRESENT_RETIRED actual_GPU_readers_complete=1 new_generated_presents=0\n");
        }
        if (FgLifecycle::driver_smooth_motion.load()) {
            release();
            return; // Never re-acquire or admit more driver interpolation after shutdown begins.
        }
    }
    // Frame generation: the interpolated frame first, the real one half a frame later (the
    // interval between real frames, smoothed).
    using Clock = std::chrono::steady_clock;
    static const bool fg_profile = std::getenv("BB_FG_PROFILE") != nullptr;
    const bool sample = fg_profile && !blank && is_game_frame && !is_reusing_frame &&
                        frame->fg_real_source && frame->fg_real_source % 31 == 0;
    const auto profile_start = Clock::now();
    auto profile_ready = profile_start;
    double generated_present_ms=0, pace_sleep_ms=0, ticket_wait_ms=0, encode_wait_ms=0;
    auto profile_ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    if (!blank && is_game_frame && !is_reusing_frame) {
        const double ms = double(frame->prepare_ns - last_prepare_ns) / 1e6;
        if (last_prepare_ns && ms > 2.0 && ms < (frame->fg_strict_source ? 1000.0 : 100.0))
            fg_interval_ms = fg_interval_ms * 0.9 + ms * 0.1;
        else fg_interval_ms = 16.667;
        last_prepare_ns = frame->prepare_ns;
    }
    if (!blank && frame->fg_ticket) {
        auto ticket = std::exchange(frame->fg_ticket, {});
        if (is_game_frame && !is_reusing_frame && ticket->previous_source &&
            last_real_game_source == ticket->previous_source) {
            const auto ticket_begin = Clock::now();
            if (ticket->ready.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
                FgLifecycle::held.store(true);
                std::fprintf(stderr,"DLSSG_PRESENT_HOLD independent consumer timeout\n");
                std::abort();
            }
            (void)ticket->ready.get(); // Actual copy/release and ACK, not merely recording.
            ticket_wait_ms=profile_ms(ticket_begin,Clock::now());
            const auto encode_begin=Clock::now();
            if (ticket->eligible->load(std::memory_order_acquire)) {
                bool encoded=true;
                for(unsigned i=0;i<ticket->generated_count;++i){
                    const auto& raw=i?ticket->additional.at(i-1):ticket->raw;
                    encoded=EncodeInterpolated(frame,vk::Image(raw->image),raw->format,raw->width,raw->height,
                        frame->fg_view_format,frame->fg_encode,present_scheduler,i)&&encoded;
                }
                frame->has_interp=encoded;frame->interp_count=ticket->generated_count;frame->fg_epoch=ticket->epoch;
                frame->interp_recorded = ticket->recorded;
                frame->interp_eligible = ticket->eligible;
                // Keep the raw image alive until its actual final encoder GPU read.
                present_scheduler.DeferPriorityOperation([lease=ticket]() mutable { lease.reset(); });
                frame->present_tick = present_scheduler.CurrentTick();
                present_scheduler.Flush(); // Required even if WSI acquisition is skipped.
                present_scheduler.Wait(frame->present_tick);
                encode_wait_ms=profile_ms(encode_begin,Clock::now());
            }
        }
    }
    if (!blank && frame->has_interp) {
        if (frame->fg_strict_source &&
            (!is_game_frame || is_reusing_frame || !frame->fg_previous_source ||
             last_real_game_source != frame->fg_previous_source)) {
            std::printf("DLSSG_PRESENT_PAIR_REJECT current=%llu expected_previous=%llu actual_previous=%llu\n",
                (unsigned long long)frame->fg_real_source,(unsigned long long)frame->fg_previous_source,
                (unsigned long long)last_real_game_source);
            frame->has_interp = false;
        }
        // Pacing starts when the images are actually ready, not before waiting for rendering.
        // This also publishes the recording result; failed dispatch copies are never counted
        // or presented as generated frames.
        WaitRendered(frame);profile_ready=Clock::now();
        frame->has_interp = frame->has_interp && frame->interp_recorded &&
                            frame->interp_recorded->load(std::memory_order_acquire) &&
                            (!frame->interp_eligible ||
                             frame->interp_eligible->load(std::memory_order_acquire));
    }
    if (sample && (ticket_wait_ms || encode_wait_ms))
        std::printf("DLSSG_PRESENT_READY_PROFILE source=%llu ticket_wait_cpu_ms=%.3f encode_record_and_wait_cpu_ms=%.3f\n",
            (unsigned long long)frame->fg_real_source,ticket_wait_ms,encode_wait_ms);
    // BB_FRAME_GEN_ONLY=1 (diagnostics): only the interpolated frames are shown.
    static const bool only_interp = [] {
        const char* value = std::getenv("BB_FRAME_GEN_ONLY");
        return value && value[0] == '1';
    }();
    if (only_interp && !blank && frame->has_interp && frame->interp[0].image) {
        frame->generated_index=1;
        frame->has_interp = false;
        PresentImage(frame, frame->interp[0].image, true, false, is_reusing_frame);
        release();
        return;
    }
    if (!blank && frame->has_interp && frame->interp[0].image) {
        frame->has_interp = false;
        auto origin=Clock::now();
        const auto step=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(
            std::clamp(fg_interval_ms/double(frame->interp_count+1),1.0,50.0)));
        for(unsigned i=0;i<frame->interp_count;++i){
            frame->generated_index=i+1;const auto gen_start=Clock::now();
            if(!PresentImage(frame,frame->interp[i].image,false,false,is_reusing_frame))break;
            if(frame->interp_count>1 && VerbosePlayerFrames())std::printf("DLSSG_MFG_PRESENT source=%llu epoch=%llu index=%u count=%u phase_denominator=%u\n",
                (unsigned long long)frame->fg_real_source,(unsigned long long)frame->fg_epoch,i+1,frame->interp_count,frame->interp_count+1);
            const auto pace_start=Clock::now();generated_present_ms+=profile_ms(gen_start,pace_start);
            // Absolute batch deadlines, with a minimum gap after a late submission.
            // Never issue a burst to catch up after a delayed Helper/WSI operation.
            auto deadline=origin+step*(i+1);
            if(frame->interp_count==1)deadline=pace_start+step; // Preserve x2 pacing.
            else if(deadline<=pace_start){origin=pace_start-step*i;deadline=pace_start+step;}
            std::this_thread::sleep_until(deadline);
            pace_sleep_ms+=profile_ms(pace_start,Clock::now());
        }
    }
    frame->generated_index=0;
    const auto real_start=Clock::now();
    PresentImage(frame, {}, true, is_game_frame && !blank, is_reusing_frame);
    if (sample && frame->fg_capture_cpu_ns)
        std::printf("FG_CAPTURE_PRESENT_PROFILE source=%llu capture_to_real_present_cpu_ms=%.3f scope=guide_capture_to_Present_return_not_input_or_scanout\n",
                    (unsigned long long)frame->fg_real_source,
                    profile_ms(Clock::time_point(std::chrono::nanoseconds(frame->fg_capture_cpu_ns)), Clock::now()));
    if (sample && frame->fg_guide_record_cpu_ns && frame->flip_queue_cpu_ns) {
        auto at=[](u64 ns){return Clock::time_point(std::chrono::nanoseconds(ns));};
        const auto now=Clock::now();
        std::printf("DLSSG_SOURCE_AGE_PROFILE source=%llu admission_wait_cpu_ms=%.3f guide_record_to_submit_cpu_ms=%.3f submit_to_flip_queue_cpu_ms=%.3f actual_present_queue_cpu_ms=%.3f present_service_cpu_ms=%.3f guide_record_to_real_present_cpu_ms=%.3f not_input_latency=1\n",
            (unsigned long long)frame->fg_real_source,
            profile_ms(at(frame->fg_capture_cpu_ns),at(frame->fg_guide_record_cpu_ns)),
            profile_ms(at(frame->fg_guide_record_cpu_ns),at(frame->fg_producer_submit_cpu_ns)),
            profile_ms(at(frame->fg_producer_submit_cpu_ns),at(frame->flip_queue_cpu_ns)),
            profile_ms(at(frame->flip_queue_cpu_ns),profile_start),profile_ms(profile_start,now),
            profile_ms(at(frame->fg_guide_record_cpu_ns),now));
    }
    if(sample)std::printf("FG_PACING_PROFILE source=%llu queue_age_cpu_ms=%.3f wait_render_cpu_ms=%.3f generated_present_cpu_ms=%.3f pacing_sleep_cpu_ms=%.3f real_present_cpu_ms=%.3f total_cpu_ms=%.3f interval_estimate_ms=%.3f\n",(unsigned long long)frame->fg_real_source,profile_ms(Clock::time_point(std::chrono::nanoseconds(frame->prepare_ns)),profile_start),profile_ms(profile_start,profile_ready),generated_present_ms,pace_sleep_ms,profile_ms(real_start,Clock::now()),profile_ms(profile_start,Clock::now()),fg_interval_ms);
    release();
}

bool Presenter::PresentImage(Frame* frame, vk::Image source, bool last,
                             bool is_game_frame, bool is_reusing_frame) {
    auto& scheduler = present_scheduler;
    const u32 width = u32(window.GetWidth());
    const u32 height = u32(window.GetHeight());
    // Nothing to present to while the window is minimised (a 0x0 surface, issue #31).
    if (width == 0 || height == 0) {
        return false;
    }
    // Recreate waits for the device. Recording threads must not submit to its queues meanwhile
    // (#211); the recreation after a failed present below already uses this same mutex.
    const auto recreate = [&] {
        std::scoped_lock lock{Scheduler::submit_mutex};
        swapchain.Recreate(width, height);
    };
    // A resized window (or one minimised at the last try) needs a new swapchain.
    if (width != swapchain.GetWidth() || height != swapchain.GetHeight() ||
        !swapchain.IsPresentable()) {
        recreate();
        if (!swapchain.IsPresentable()) {
            return false;
        }
    }
    if (!swapchain.AcquireNextImage()) {
        recreate();
        if (!swapchain.AcquireNextImage()) {
            // Resized faster than the swapchain can follow: this frame is skipped.
            LOG_WARNING(Render_Vulkan, "Skipping a frame: no swapchain image");
            return false;
        }
    }

    const bool blank = frame == &blank_frame;
    const vk::Extent2D extent = swapchain.GetExtent();
    SetExpectedSize(extent.width, extent.height);
    // Reset here, not when the frame is taken: a frame skipped above keeps its fence signaled.
    if (!blank && last) {
        Check<"reset a frame fence">(instance.GetDevice().resetFences(frame->present_done));
    }

    const auto cmdbuf = present_scheduler.CommandBuffer();
    // Interpolation, reuse and blanks never publish a new NR game-source identity.
    if (is_game_frame && !is_reusing_frame && !blank && !source && last && frame->runtime_capture)
        RecordRuntimeOutput(instance, scheduler, frame->image, frame->image_format,
                            frame->width, frame->height, *frame->runtime_capture, "Present");
    if (!blank && last) {
        DumpFinalFrameIfDue(instance, present_scheduler, cmdbuf, frame->image, frame->width,
                            frame->height, swapchain.GetSurfaceFormat().format);
    }
    // BB_SEQUENCE_DUMP_TRIGGER=<file> (frame generation checks): the next 16 images shown, the
    // interpolated ones ("interp") and the game's ("real") in the order they are presented.
    {
        static const char* trigger = std::getenv("BB_SEQUENCE_DUMP_TRIGGER");
        static int left = 0, index = 0;
        if (trigger && left == 0 && std::remove(trigger) == 0) {
            left = 16;
            index = 0;
        }
        if (left > 0 && !blank) {
            --left;
            std::printf("FG_SEQUENCE_CAPTURE index=%d source=%llu generated_index=%u generated_count=%u epoch=%llu\n",index,(unsigned long long)frame->fg_real_source,source?frame->generated_index:0,frame->interp_count,(unsigned long long)frame->fg_epoch);
            DumpPresented(instance, present_scheduler, cmdbuf, source ? source : frame->image,
                          frame->width, frame->height, swapchain.GetSurfaceFormat().format, index++,
                          source ? "interp" : "real");
        }
    }
    RecordPresent(cmdbuf, blank ? nullptr : frame, swapchain.Image(), extent, source);

    // The frame's submission goes out from a recording thread (BB_ASYNC_SUBMIT); the present
    // one, waiting for it on the same queue, must not get there first.
    if (!blank && frame->ready_semaphore == draw_scheduler.GetWorkSemaphore()->Handle()) {
        draw_scheduler.WaitSubmitted(frame->ready_tick);
    }
    SubmitInfo info{};
    info.AddWait(swapchain.GetImageAcquiredSemaphore());
    if (!blank) {
        info.AddWait(frame->ready_semaphore, frame->ready_tick);
        if (last) {
            info.AddSignal(frame->present_done);
        }
    }
    info.AddSignal(swapchain.GetPresentReadySemaphore());
    const u64 present_submit_tick = scheduler.CurrentTick();
    present_scheduler.Flush(info);
    swapchain.MarkAcquireSubmitted(present_scheduler.GetWorkSemaphore()->Handle(), present_submit_tick);
    if (!blank) frame->present_tick = present_submit_tick;
    {
        std::scoped_lock lock{Scheduler::submit_mutex};
        Scheduler::Q0SubmitOwnerScope q0_owner{&scheduler, 6};
        q0_owner.Stage(15);
        const bool presented = swapchain.Present();
        if (!presented) ++present_failed;
        else if (source) ++present_generated;
        else if (is_game_frame && !is_reusing_frame && !blank) {
            ++present_real;
            last_real_game_source = frame->fg_real_source;
        }
        else ++present_other;
        FgLifecycle::real_presents.store(present_real, std::memory_order_relaxed);
        FgLifecycle::generated_presents.store(present_generated, std::memory_order_relaxed);
        FgLifecycle::other_presents.store(present_other, std::memory_order_relaxed);
        static const bool fg_audit = [] {
            const char* value = std::getenv("BB_FRAME_GEN_AUDIT");
            return value && value[0] == '1';
        }();
        if (fg_audit) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::printf("FG_PRESENT kind=%s source_frame=%llu fg_frame=%llu ready_tick=%llu submit_tick=%llu success=%u real=%llu generated=%llu other=%llu failed=%llu cpu_monotonic_ns=%lld source_game_frame=%llu generated_index=%u generated_count=%u fg_epoch=%llu nr_source_game_frame=%llu scope=present_return_not_scanout\n",
                source ? "generated" : (!blank && is_game_frame && !is_reusing_frame ? "real" : "other"),
                (unsigned long long)frame->c1_present_frame_number,
                (unsigned long long)frame->fg_frame, (unsigned long long)frame->ready_tick,
                (unsigned long long)present_submit_tick, presented,
                (unsigned long long)present_real, (unsigned long long)present_generated,
                (unsigned long long)present_other, (unsigned long long)present_failed, (long long)ns,
                (unsigned long long)frame->fg_real_source, source ? frame->generated_index : 0, frame->interp_count,(unsigned long long)frame->fg_epoch,(unsigned long long)frame->c1_source_game_frame);
        }
        if(is_game_frame&&!is_reusing_frame&&frame->runtime_capture)RuntimeCapturePresented(*frame->runtime_capture,presented);
        q0_owner.Stage(16);
        const auto c4_now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        static const bool memory_profile = [] {
            const char* value = std::getenv("BB_MEMORY_PROFILE");
            return value && std::strcmp(value, "1") == 0;
        }();
        if (memory_profile && !source && !blank && is_game_frame && !is_reusing_frame) {
            // Same point for native, SR, NR and FG: successful real Present return.
            // Neither a GPU completion assertion nor a physical scanout timestamp.
            std::printf("MEMORY_PRESENT_V1 model=%s frame=%llu ready_tick=%llu "
                        "present_tick=%llu success=%u cpu_monotonic_ns=%lld "
                        "scope=real_present_return_not_gpu_completion\n",
                        VideoCore::GuestInPlace() ? "new" : "old",
                        (unsigned long long)frame->c1_present_frame_number,
                        (unsigned long long)frame->ready_tick,
                        (unsigned long long)present_submit_tick, presented, (long long)c4_now_ns);
        }
        const char* p0_mode=std::getenv("BB_NR_P0_MODE");
        if(is_game_frame && !is_reusing_frame && p0_mode && std::strcmp(p0_mode,"srOnly")==0)
            std::printf("P0_SR_PRESENT mode=srOnly actual_game_frame=1 reused=0 cpu_monotonic_ns=%lld present_frame_number=%llu swapchain_present=%u helper_started=0 IPC=0 scope=CPU_present_call_completed_not_input_to_photon\n",
                (long long)c4_now_ns,(unsigned long long)BbStats::frame_number.load(),presented);
        // Distinct base-game ledger for resident off/startup/fallback. This checks
        // actual Frame selection, not requested_enable or assumed Helper state.
        const bool selected_p0 = frame->audit_stage == "P0" && frame->audit_selected_output;
        if (SrTimingMath::BaseSrPresent(is_game_frame,is_reusing_frame,
                p0_mode && std::strcmp(p0_mode,"resident")==0,
                frame->p0_selection_known && frame->c1_present_frame_number!=0,selected_p0)) {
            const char* base_session=std::getenv("BB_NR_P0_SESSION");
            std::printf("P0_BASE_SR_PRESENT mode=resident base_session=%s actual_game_frame=1 reused=0 selected_P0_NR=0 selection_known=1 cpu_monotonic_ns=%lld present_frame_number=%llu ready_tick=%llu present_submit_tick=%llu swapchain_present=%u scope=CPU_present_call_completed_not_photon actual_selection_scope=prepared_Frame_no_P0_NR_selection_not_proof_of_SR_dispatch Helper_state=NOT_INFERRED IPC=NOT_INFERRED\n",
                base_session?base_session:"NOT_AVAILABLE",(long long)c4_now_ns,
                (unsigned long long)frame->c1_present_frame_number,(unsigned long long)frame->ready_tick,
                (unsigned long long)present_submit_tick,presented);
        }
        if (is_game_frame && !is_reusing_frame && std::getenv("BB_NR_C4_BASELINE"))
            std::printf("C4_SR_PRESENT mode=srOnly actual_game_frame=1 reused=0 cpu_monotonic_ns=%lld present_frame_number=%llu swapchain_present=%u helper_started=0 IPC=0\n",
                (long long)c4_now_ns, (unsigned long long)BbStats::frame_number.load(), presented);
        if(presented&&is_game_frame&&!is_reusing_frame&&frame->audit_stage=="NATIVE_ASYNC_PRE_SR"&&frame->c1_source_game_frame)
            BbNR::NotifyPresent(frame->c1_source_game_frame,frame->c1_present_frame_number,c4_now_ns);
        if(VerbosePlayerFrames()&&is_game_frame&&!is_reusing_frame&&frame->audit_stage=="NATIVE_ASYNC_PRE_SR"&&frame->c1_source_game_frame)
            std::printf("NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT session=%s source_game_frame=%llu present_frame_number=%llu merge_tick=%llu prepare_tick=%llu ready_tick=%llu swapchain_present=%u cpu_monotonic_ns=%lld raw_or_cache720_SR1080=1 cache_selected=%u job=%u current_NR_inference_claim=0 pixel_change=NOT_MEASURED_HOT\n",frame->audit_session.c_str(),(unsigned long long)frame->c1_source_game_frame,(unsigned long long)frame->c1_present_frame_number,(unsigned long long)frame->c1_merge_tick,(unsigned long long)frame->c1_guest_prepare_tick,(unsigned long long)frame->ready_tick,presented,(long long)c4_now_ns,frame->audit_selected_output,frame->audit_nr_frame);
        if(VerbosePlayerFrames()&&is_game_frame&&!is_reusing_frame&&frame->audit_stage=="NATIVE_ASYNC"&&frame->c1_source_game_frame)
            std::printf("NATIVE_ASYNC_CURRENT_PRESENT session=%s snapshot_scope=%s source_game_frame=%llu present_frame_number=%llu merge_tick=%llu prepare_tick=%llu ready_tick=%llu swapchain_present=%u cpu_monotonic_ns=%lld current_SR=1 cache_selected=%u effect_applied=%u sparse_job=%u source_edited=0 current_NR_inference_claim=0\n",
                frame->audit_session.c_str(),frame->audit_mode.c_str(),(unsigned long long)frame->c1_source_game_frame,
                (unsigned long long)frame->c1_present_frame_number,(unsigned long long)frame->c1_merge_tick,
                (unsigned long long)frame->c1_guest_prepare_tick,(unsigned long long)frame->ready_tick,presented,(long long)c4_now_ns,frame->audit_selected_output,frame->audit_mode=="sparse-NR-Sky-current",frame->audit_nr_frame);
        if (is_game_frame && !is_reusing_frame && frame->audit_stage == "C4" && frame->c1_source_game_frame)
            std::printf("C4_PRESENT_CPU_TIME session=%s frame=%u source_game_frame=%llu cpu_monotonic_ns=%lld selected_output=%u swapchain_present=%u scope=CPU_present_call_completed_not_input_to_photon\n",
                frame->audit_session.c_str(), frame->audit_nr_frame, (unsigned long long)frame->c1_source_game_frame,
                (long long)c4_now_ns, frame->audit_selected_output, presented);
        if (frame->c1_source_game_frame != 0 && is_game_frame && !is_reusing_frame &&
            frame->audit_stage == "P0") {
            std::printf("P0_PRESENT_SUBMITTED stage=P0 execution_mode=%s mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu present_submit_tick=%llu swapchain_present=%u audit_kind=%s cpu_monotonic_ns=%lld scope=CPU_present_call_completed_not_input_to_photon\n",
                frame->audit_execution_mode.c_str(), frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
                frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
                (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
                (unsigned long long)frame->c1_guest_prepare_tick, (unsigned long long)frame->ready_tick,
                (unsigned long long)frame->c1_present_frame_number, (unsigned long long)present_submit_tick,
                presented, frame->audit_full ? "full" : "off", (long long)c4_now_ns);
        }
        if (frame->c1_source_game_frame != 0 && is_game_frame && !is_reusing_frame &&
            frame->audit_stage == "Q1") {
            std::printf("Q1_PRESENT_SUBMITTED stage=Q1 execution_mode=%s mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu present_submit_tick=%llu swapchain_present=%u audit_kind=%s\n",
                frame->audit_execution_mode.c_str(), frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
                frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
                (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
                (unsigned long long)frame->c1_guest_prepare_tick, (unsigned long long)frame->ready_tick,
                (unsigned long long)frame->c1_present_frame_number, (unsigned long long)present_submit_tick,
                presented, frame->audit_full ? "full" : "off");
        } else if (frame->c1_source_game_frame != 0 && is_game_frame && !is_reusing_frame &&
            frame->audit_stage == "C4") {
            std::printf("C4_PRESENT_SUBMITTED stage=C4 execution_mode=%s mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu present_submit_tick=%llu swapchain_present=%u audit_kind=%s\n",
                frame->audit_execution_mode.c_str(), frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
                frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
                (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
                (unsigned long long)frame->c1_guest_prepare_tick, (unsigned long long)frame->ready_tick,
                (unsigned long long)frame->c1_present_frame_number, (unsigned long long)present_submit_tick,
                presented, frame->audit_full ? "full" : "off");
        } else if (frame->c1_source_game_frame != 0 && is_game_frame && !is_reusing_frame &&
            frame->audit_stage == "C3") {
            std::printf("C3_PRESENT_SUBMITTED stage=C3 mode=%s session=%s frame=%u selected_output=%u source_game_frame=%llu merge_tick=%llu guest_buffer_address=0x%llx guest_prepare_tick=%llu ready_tick=%llu present_frame_number=%llu present_submit_tick=%llu swapchain_present=%u audit_kind=%s\n",
                frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_nr_frame,
                frame->audit_selected_output, (unsigned long long)frame->c1_source_game_frame,
                (unsigned long long)frame->c1_merge_tick, (unsigned long long)frame->c1_guest_buffer_address,
                (unsigned long long)frame->c1_guest_prepare_tick, (unsigned long long)frame->ready_tick,
                (unsigned long long)frame->c1_present_frame_number, (unsigned long long)present_submit_tick,
                presented, frame->audit_full ? "full" : "gpu_full_scan");
        } else if (frame->c1_source_game_frame != 0 && is_game_frame && frame->audit_stage == "C2") {
            std::printf("C2_PRESENT_SUBMITTED stage=C2 mode=%s session=%s selected_output=%u "
                        "route=nonupscaled_videoout source_game_frame=%llu merge_tick=%llu "
                        "guest_buffer_address=0x%llx guest_prepare_gpu_frame=%llu guest_prepare_tick=%llu "
                        "present_frame_number=%llu frame_ready_tick=%llu present_submit_tick=%llu "
                        "game_frame=%u swapchain_present=%u\n",
                        frame->audit_mode.c_str(), frame->audit_session.c_str(), frame->audit_selected_output,
                        static_cast<unsigned long long>(frame->c1_source_game_frame),
                        static_cast<unsigned long long>(frame->c1_merge_tick),
                        static_cast<unsigned long long>(frame->c1_guest_buffer_address),
                        static_cast<unsigned long long>(frame->c1_guest_prepare_gpu_frame),
                        static_cast<unsigned long long>(frame->c1_guest_prepare_tick),
                        static_cast<unsigned long long>(frame->c1_present_frame_number),
                        static_cast<unsigned long long>(frame->ready_tick),
                        static_cast<unsigned long long>(present_submit_tick), is_game_frame, presented);
        } else if (frame->c1_source_game_frame != 0 && is_game_frame && frame->audit_stage != "NATIVE_ASYNC" && frame->audit_stage != "NATIVE_ASYNC_PRE_SR") {
            std::printf("C1_PRESENT_SUBMITTED route=nonupscaled_videoout source_game_frame=%llu merge_tick=%llu "
                        "guest_buffer_address=0x%llx guest_prepare_gpu_frame=%llu "
                        "guest_prepare_tick=%llu present_frame_number=%llu "
                        "frame_ready_tick=%llu present_submit_tick=%llu "
                        "game_frame=%u swapchain_present=%u\n",
                        static_cast<unsigned long long>(frame->c1_source_game_frame),
                        static_cast<unsigned long long>(frame->c1_merge_tick),
                        static_cast<unsigned long long>(frame->c1_guest_buffer_address),
                        static_cast<unsigned long long>(frame->c1_guest_prepare_gpu_frame),
                        static_cast<unsigned long long>(frame->c1_guest_prepare_tick),
                        static_cast<unsigned long long>(frame->c1_present_frame_number),
                        static_cast<unsigned long long>(frame->ready_tick),
                        static_cast<unsigned long long>(present_submit_tick), is_game_frame,
                        presented);
        }
        if (!presented) {
            swapchain.Recreate(width, height);
            return false;
        }
    }
    return true;
}

} // namespace Vulkan

extern "C" float bbgpu_frame_aspect(void) {
    return Vulkan::shown_aspect.load(std::memory_order_relaxed);
}
