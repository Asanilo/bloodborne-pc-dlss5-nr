// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "buffers/buffer.h"
#include "render/scheduler.h"
#include "upscale/dlssg_camera.h"
#include "upscale/dlssg_gpu_peer.h"
#include "upscale/dlssg_native_timing.h"
#include "upscale/frame_generation_lifecycle.h"
namespace Vulkan {
class DlssgGpuBridge {
  using J = DlssgGpu::J;
  using Clock = std::chrono::steady_clock;
  const Instance &instance;
  Scheduler &scheduler;
  std::unique_ptr<DlssgGpu::MailboxImport> mailbox;
  std::unique_ptr<DlssgGpu::Peer> peer;
  std::unique_ptr<VideoCore::Buffer> depth_transfer;
  DlssgNativeTiming timing;
  uint64_t sequence = 0, epoch = 0, last_source = 0, helper_serial = 0,
           guide_source = 0, last_recorded_tick = 0;
  bool guides = false, pending = false, retired = false;
  J last_request, response;
  DlssgCamera::Camera previous_camera, current_camera;
  Clock::time_point last_completed{};
  static void Need(bool v, const char *why) { DlssgGpu::Need(v, why); }
  [[noreturn]] void Fail(const std::exception &e) {
    FgLifecycle::held.store(true);
    std::fprintf(
        stderr,
        "DLSSG_GPU_NATIVE_HOLD functional=FAIL reason=%s forced_retirement=0\n",
        e.what());
    std::fflush(stderr);
    std::abort();
  }
  static void SourceBarrier(vk::CommandBuffer cmd, vk::Image image,
                            vk::ImageAspectFlags aspect, bool before) {
    const vk::ImageMemoryBarrier2 b{
        .srcStageMask = before ? vk::PipelineStageFlagBits2::eAllCommands
                               : vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = before ? vk::AccessFlagBits2::eMemoryRead |
                                      vk::AccessFlagBits2::eMemoryWrite
                                : vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = before ? vk::PipelineStageFlagBits2::eCopy
                               : vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = before ? vk::AccessFlagBits2::eTransferRead
                                : vk::AccessFlagBits2::eMemoryRead |
                                      vk::AccessFlagBits2::eMemoryWrite,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspect, 0, 1, 0, 1}};
    cmd.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
  }
  void Start() {
    if (peer)
      return;
    scheduler.Finish();
    const char *path = std::getenv("BB_DLSSG_STREAM_CONFIG");
    Need(path && *path, "GPU bridge explicit config");
    auto config = DlssgGpu::Peer::Read(path);
    Need(config.at("schema") == "dlssg-native-gpu-v1" &&
             config.at("transport") == "gpu-dmabuf-v1",
         "GPU bridge sealed transport");
    const char *root = std::getenv("BB_DLSSG_STREAM_DIR");
    Need(root && config.at("ipc") == root, "GPU bridge exact IPC root");
    mailbox = std::make_unique<DlssgGpu::MailboxImport>(
        VkInstance(instance.GetInstance()),
        VkPhysicalDevice(instance.GetPhysicalDevice()),
        VkDevice(instance.GetDevice()), instance.GetGraphicsQueueFamilyIndex(),
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr);
    peer = std::make_unique<DlssgGpu::Peer>(config);
    auto ready = peer->Start(*mailbox);
    helper_serial = DlssgGpu::U(ready, "helper_serial");
    last_request = J{{"schema", DlssgGpu::Schema},
                     {"session", peer->session},
                     {"sequence", 0},
                     {"epoch", 0},
                     {"source_game_frame", 0},
                     {"previous_source_game_frame", 0},
                     {"display", 0},
                     {"native_input_tick", 0}};
    depth_transfer = std::make_unique<VideoCore::Buffer>(
        instance, 0, 1280u * 720 * 4, VideoCore::MemoryType::DeviceLocal,
        "DLSSG depth GPU transfer");
    std::printf("DLSSG_GPU_BRIDGE_READY session=%s imported_images=4 "
                "export_raw_fds_retired=1 pixel_host_transfer_bytes=0\n",
                peer->session.c_str());
  }
  void Observe() {
    if (last_recorded_tick > mailbox->last_use) {
      Need(scheduler.IsFree(last_recorded_tick),
           "GPU mailbox actual Native last use");
      mailbox->Observed(last_recorded_tick);
    }
  }

public:
  DlssgGpuBridge(const Instance &i, Scheduler &s)
      : instance(i), scheduler(s), timing(i) {}
  ~DlssgGpuBridge() {
    if (peer && !retired) {
      FgLifecycle::held.store(true);
      // Unknown Native last use: retain the backing GPU objects through process
      // teardown.
      (void)depth_transfer.release();
      (void)timing.pool.release();
      std::fprintf(stderr, "DLSSG_GPU_NATIVE_HOLD normal_retirement_missing=1 "
                           "retained_GPU_owners=1\n");
    }
  }
  uint64_t LastSource() const { return last_source; }
  bool Guides(const FrameGeneration::Scene &scene) {
    if (scene.width != 1920 || scene.height != 1080 ||
        scene.render_width != 1280 || scene.render_height != 720 ||
        !scene.source_game_frame || !scene.depth || !scene.motion ||
        (scene.depth_format != vk::Format::eD32Sfloat &&
         scene.depth_format != vk::Format::eD32SfloatS8Uint) ||
        (scene.color_format != vk::Format::eR8G8B8A8Unorm &&
         scene.color_format != vk::Format::eB8G8R8A8Unorm))
      return false;
    try {
      Start();
      Need(!pending, "GPU previous response consumed");
      if (guides) {
        scheduler.Finish();
        Observe();
      }
      guides = true;
      guide_source = scene.source_game_frame;
      timing.source = guide_source;
      timing.guides = Clock::now();
      scheduler.EndRendering();
      last_recorded_tick = scheduler.CurrentTick();
      mailbox->MarkUse();
      scheduler.Record(
          [this, depth = scene.depth, motion = scene.motion,
           buffer = depth_transfer->Handle()](vk::CommandBuffer cmd) {
            timing.Mark(cmd, 0);
            mailbox->Ownership(VkCommandBuffer(cmd), 1, true, true);
            SourceBarrier(cmd, depth, vk::ImageAspectFlagBits::eDepth, true);
            // D32/D32S8 depth aspect -> device-only bytes -> R32F color aspect.
            vk::BufferImageCopy cp{
                .imageSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
                .imageExtent = {1280, 720, 1}};
            cmd.copyImageToBuffer(depth, vk::ImageLayout::eGeneral, buffer, cp);
            const vk::BufferMemoryBarrier2 b{
                .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
                .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer,
                .size = VK_WHOLE_SIZE};
            cmd.pipelineBarrier2(
                {.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &b});
            cp.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            cmd.copyBufferToImage(buffer, vk::Image(mailbox->images[1].image),
                                  vk::ImageLayout::eGeneral, cp);
            SourceBarrier(cmd, depth, vk::ImageAspectFlagBits::eDepth, false);
            mailbox->Ownership(VkCommandBuffer(cmd), 1, false, true);
            mailbox->Ownership(VkCommandBuffer(cmd), 2, true, true);
            SourceBarrier(cmd, motion, vk::ImageAspectFlagBits::eColor, true);
            const vk::ImageCopy copy{
                .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .extent = {1280, 720, 1}};
            cmd.copyImage(motion, vk::ImageLayout::eGeneral,
                          vk::Image(mailbox->images[2].image),
                          vk::ImageLayout::eGeneral, copy);
            SourceBarrier(cmd, motion, vk::ImageAspectFlagBits::eColor, false);
            mailbox->Ownership(VkCommandBuffer(cmd), 2, false, true);
            timing.Mark(cmd, 1);
          });
      return true;
    } catch (const std::exception &e) {
      Fail(e);
    }
  }
  bool Evaluate(const FrameGeneration::Scene &scene, vk::Image color,
                VAddr display, bool reset) {
    try {
      Need(guides && !pending && guide_source == scene.source_game_frame,
           "GPU same real guides/color");
      const auto started = Clock::now();
      scheduler.EndRendering();
      last_recorded_tick = scheduler.CurrentTick();
      mailbox->MarkUse();
      scheduler.Record([this, color](vk::CommandBuffer cmd) {
        timing.Mark(cmd, 2);
        mailbox->Ownership(VkCommandBuffer(cmd), 0, true, true);
        SourceBarrier(cmd, color, vk::ImageAspectFlagBits::eColor, true);
        const vk::ImageCopy copy{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {1920, 1080, 1}};
        cmd.copyImage(color, vk::ImageLayout::eGeneral,
                      vk::Image(mailbox->images[0].image),
                      vk::ImageLayout::eGeneral, copy);
        SourceBarrier(cmd, color, vk::ImageAspectFlagBits::eColor, false);
        mailbox->Ownership(VkCommandBuffer(cmd), 0, false, true);
        timing.Mark(cmd, 3);
      });
      const auto recorded = Clock::now();
      scheduler.Finish();
      const auto completed = Clock::now();
      Observe();
      timing.ReadInput(DlssgNativeTiming::Ms(started, recorded),
                       DlssgNativeTiming::Ms(recorded, completed), 0, true);
      guides = false;
      reset = reset || !last_source ||
              scene.source_game_frame != last_source + 1 ||
              (last_completed != Clock::time_point{} &&
               completed - last_completed > std::chrono::milliseconds(250));
      if (reset)
        ++epoch;
      current_camera = DlssgCamera::Prepare(
          scene, last_source ? &previous_camera : nullptr, reset);
      last_request = J{{"schema", DlssgGpu::Schema},
                       {"session", peer->session},
                       {"sequence", sequence + 1},
                       {"epoch", epoch},
                       {"source_game_frame", scene.source_game_frame},
                       {"previous_source_game_frame", last_source},
                       {"reset", reset},
                       {"display", display},
                       {"wire_order", "game-scene-bytes"},
                       {"transport", "gpu-dmabuf-v1"},
                       {"native_input_tick", mailbox->last_use},
                       {"native_input_GPU_complete", true},
                       {"camera", current_camera.constants}};
      const auto wait_start = Clock::now();
      response = peer->Exchange(last_request, helper_serial);
      helper_serial = DlssgGpu::U(response, "helper_serial");
      pending = true;
      if (std::getenv("BB_FG_PROFILE") && (sequence + 1) % 120 == 0) {
        auto profile = response;
        profile.erase("camera");
        profile["native_wait_cpu_ms"] =
            DlssgNativeTiming::Ms(wait_start, Clock::now());
        profile["producer_finish_cpu_ms"] =
            DlssgNativeTiming::Ms(recorded, completed);
        std::printf("DLSSG_GPU_BRIDGE_PROFILE %s\n", profile.dump().c_str());
      }
      const bool eligible = DlssgGpu::B(response, "eligible");
      if (eligible)
        Need(DlssgGpu::U(response, "result") == 1 &&
                 (DlssgGpu::U(response, "disable_word") & 255) == 0,
             "GPU model output eligibility");
      return eligible;
    } catch (const std::exception &e) {
      Fail(e);
    }
  }
  void Consume(vk::Image target,
               const std::shared_ptr<std::atomic<bool>> &recorded = {}) {
    try {
      Need(pending, "GPU response before Native consume");
      scheduler.EndRendering();
      last_recorded_tick = scheduler.CurrentTick();
      mailbox->MarkUse();
      const auto tick = last_recorded_tick;
      timing.upload_source = DlssgGpu::U(last_request, "source_game_frame");
      timing.upload_pending = bool(timing.pool);
      scheduler.Record([this, target, recorded](vk::CommandBuffer cmd) {
        timing.Mark(cmd, 4);
        mailbox->Ownership(VkCommandBuffer(cmd), 3, true, false);
        if (target) {
          const vk::ImageMemoryBarrier2 before{
              .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
              .srcAccessMask = vk::AccessFlagBits2::eMemoryRead |
                               vk::AccessFlagBits2::eMemoryWrite,
              .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
              .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
              .oldLayout = vk::ImageLayout::eUndefined,
              .newLayout = vk::ImageLayout::eGeneral,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = target,
              .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                   1}};
          cmd.pipelineBarrier2(
              {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
          const vk::ImageCopy copy{
              .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
              .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
              .extent = {1920, 1080, 1}};
          cmd.copyImage(vk::Image(mailbox->images[3].image),
                        vk::ImageLayout::eGeneral, target,
                        vk::ImageLayout::eGeneral, copy);
          auto after = before;
          after.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
          after.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
          after.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
          after.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
          after.oldLayout = vk::ImageLayout::eGeneral;
          cmd.pipelineBarrier2(
              {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
        }
        mailbox->Ownership(VkCommandBuffer(cmd), 3, false, false);
        timing.Mark(cmd, 5);
        if (recorded)
          recorded->store(true, std::memory_order_release);
      });
      scheduler.Finish();
      Observe();
      peer->Consume(last_request, helper_serial, tick);
      sequence = DlssgGpu::U(last_request, "sequence");
      last_source = DlssgGpu::U(last_request, "source_game_frame");
      previous_camera = std::move(current_camera);
      last_completed = Clock::now();
      pending = false;
      if (sequence <= 3 || sequence % 120 == 0)
        std::printf("DLSSG_GPU_NATIVE_OUTPUT sequence=%llu source=%llu "
                    "epoch=%llu eligible=%u Native_copy_tick=%llu "
                    "helper_serial=%llu pixel_host_transfer_bytes=0\n",
                    (unsigned long long)sequence,
                    (unsigned long long)last_source, (unsigned long long)epoch,
                    unsigned(bool(target)), (unsigned long long)tick,
                    (unsigned long long)helper_serial);
    } catch (const std::exception &e) {
      Fail(e);
    }
  }
  bool Retire() {
    if (retired)
      return true;
    if (!peer) {
      retired = true;
      std::printf("DLSSG_GPU_BRIDGE_NO_PEER no_helper_created=1\n");
      return true;
    }
    try {
      Need(!pending, "GPU completed request before stop");
      scheduler.Finish();
      Observe();
      peer->Retire(*mailbox, last_request, helper_serial);
      depth_transfer.reset();
      retired = true;
      std::printf(
          "DLSSG_GPU_BRIDGE_RETIRED helper_GPU_complete=1 feature_released=1 "
          "child_reaped=1 Native_shared_GPU_complete=1 imports_destroyed=1 "
          "sequence=%llu\n",
          (unsigned long long)sequence);
      return true;
    } catch (const std::exception &e) {
      FgLifecycle::held.store(true);
      std::fprintf(stderr,
                   "DLSSG_GPU_NATIVE_HOLD functional=FAIL retirement=%s\n",
                   e.what());
      return false;
    }
  }
};
} // namespace Vulkan
