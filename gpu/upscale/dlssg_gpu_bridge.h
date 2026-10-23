// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "buffers/buffer.h"
#include "render/scheduler.h"
#include "upscale/dlssg_camera.h"
#include "upscale/dlssg_gpu_peer.h"
#include "upscale/dlssg_native_timing.h"
#include "upscale/frame_generation_lifecycle.h"
#include <future>
#include "upscale/dlssg_output_ticket.h"
#include "upscale/dlssg_serial_worker.h"
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
  std::future<double> consumed_ack;
  uint64_t consumed_tick = 0;
  J last_request, response;
  bool async_exchange = false, late_exchange = false;
  bool independent_consumer = false;
  std::unique_ptr<Scheduler> consumer_scheduler; // Construct/record/destroy on service thread only.
  std::atomic<u64> consumer_completed_tick{0};
  std::array<std::shared_ptr<DlssgRawOutput>, 10> raw_outputs;
  unsigned generated_count = 1;
  std::shared_ptr<DlssgOutputTicket> output_ticket;
  u64 slot_misses = 0;
  std::packaged_task<void()> deferred_job;
  std::unique_ptr<DlssgSerialWorker> worker;
  std::shared_future<J> flight;
  std::shared_ptr<std::atomic<bool>> flight_eligible;

  void JoinFlight() {
    if (!flight.valid()) return;
    const auto begin = Clock::now();
    Need(flight.wait_for(std::chrono::seconds(30)) == std::future_status::ready,
         "GPU submission prerequisite response deadline");
    response = flight.get();
    helper_serial = DlssgGpu::U(response, "helper_serial");
    if (std::getenv("BB_FG_PROFILE") && sequence % 31 == 0)
      std::printf("DLSSG_GPU_SUBMIT_JOIN sequence=%llu source=%llu owner_join_cpu_ms=%.3f extra_game_frame_buffer=0\n",
                  (unsigned long long)sequence, (unsigned long long)last_source, DlssgNativeTiming::Ms(begin, Clock::now()));
    flight = {};
  }
  DlssgCamera::Camera previous_camera, current_camera;
  Clock::time_point last_completed{};
  static void Need(bool v, const char *why) { DlssgGpu::Need(v, why); }
  [[noreturn]] static void Fail(const std::exception &e) {
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
    generated_count=config.value("multiplier",2u)-1;
    Need(generated_count>=1&&generated_count<=5,"bounded MFG output count");
    Need(generated_count==1||config.at("pipeline")=="consumer-queue-v1","MFG requires Q1");
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
    const auto pipeline = config.value("pipeline", std::string("sync-v1"));
    Need(pipeline == "sync-v1" || pipeline == "submit-ready-v1" || pipeline == "late-ready-v1" || pipeline == "consumer-ready-v1" || pipeline == "consumer-queue-v1", "supported GPU pipeline");
    async_exchange = pipeline != "sync-v1";
    late_exchange = pipeline == "late-ready-v1";
    independent_consumer = pipeline == "consumer-ready-v1" || pipeline == "consumer-queue-v1";
    if (async_exchange) {
      worker = std::make_unique<DlssgSerialWorker>();
    }
    std::printf("DLSSG_GPU_PIPELINE mode=%s async_submit_prerequisite=%u external_semaphore=0 extra_game_frame_buffer=0\n",
                pipeline.c_str(), unsigned(async_exchange));
    last_request = J{{"generated_count",generated_count},{"schema", DlssgGpu::Schema},
                     {"session", peer->session},
                     {"sequence", 0},
                     {"epoch", 0},
                     {"source_game_frame", 0},
                     {"previous_source_game_frame", 0},
                     {"display", 0},
                     {"native_input_tick", 0}};
    if (independent_consumer) {
      last_request["native_completion_mode"] = "split-v1";
      last_request["native_input_domain"] = "draw";
    }
    depth_transfer = std::make_unique<VideoCore::Buffer>(
        instance, 0, 1280u * 720 * 4, VideoCore::MemoryType::DeviceLocal,
        "DLSSG depth GPU transfer");
    std::printf("DLSSG_GPU_BRIDGE_READY session=%s imported_images=%u "
                "export_raw_fds_retired=1 pixel_host_transfer_bytes=0\n",
                peer->session.c_str(), 3+generated_count);
  }
  void Observe() {
    if (last_recorded_tick > mailbox->last_use) {
      Need(scheduler.IsFree(last_recorded_tick),
           "GPU mailbox actual Native last use");
      mailbox->Observed(last_recorded_tick);
    }
  }

  void WaitConsumedAck() {
    if (!consumed_ack.valid())
      return;
    const auto start = Clock::now();
    Need(consumed_ack.wait_for(std::chrono::seconds(30)) == std::future_status::ready,
         "GPU asynchronous consumer ACK deadline");
    const double publish_ms =
        consumed_ack.get(); // Propagates callback failure to HOLD.
    Need(scheduler.IsFree(consumed_tick),
         "GPU asynchronous consumer actual completion");
    if (std::getenv("BB_FG_PROFILE") && sequence % 31 == 0)
      std::printf("DLSSG_GPU_ACK_PROFILE sequence=%llu source=%llu "
                  "Native_copy_tick=%llu join_cpu_ms=%.3f publish_cpu_ms=%.3f "
                  "actual_GPU_complete=1\n",
                  (unsigned long long)sequence, (unsigned long long)last_source,
                  (unsigned long long)consumed_tick,
                  DlssgNativeTiming::Ms(start, Clock::now()), publish_ms);
  }

public:
  DlssgGpuBridge(const Instance &i, Scheduler &s)
      : instance(i), scheduler(s), timing(i) {}
  ~DlssgGpuBridge() {
    if (peer && !retired) {
      if (worker)
        Fail(std::runtime_error("unretired GPU submission prerequisite owner destruction"));
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
  bool Late() const { return late_exchange; }
  bool Independent() const { return independent_consumer; }
  std::shared_ptr<DlssgOutputTicket> TakeTicket() { return std::exchange(output_ticket, {}); }
  std::shared_ptr<DlssgRawOutput> ClaimOutput(vk::Format format) {
    for (unsigned index=0;index<2*generated_count;++index) {
      auto& slot=raw_outputs[index];
      if (!slot) {
        slot = std::make_shared<DlssgRawOutput>();
        slot->format = format;
        slot->image = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        slot->image.Create({.flags = vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage,
          .imageType = vk::ImageType::e2D, .format = format, .extent = {1920,1080,1},
          .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
          .tiling = vk::ImageTiling::eOptimal,
          .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled});
      }
      if (slot.use_count() == 1) { Need(slot->format == format,"fixed raw output format"); return slot; }
    }
    if (++slot_misses <= 3 || slot_misses % 120 == 0)
      std::printf("DLSSG_RAW_SLOT_BUSY skipped=%llu GPU_reader_lease_retained=1\n",(unsigned long long)slot_misses);
    return {};
  }

  std::shared_ptr<std::atomic<bool>> Eligibility() const { return flight_eligible; }
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
      Need(!deferred_job.valid(), "GPU dormant request resolved before guide reuse");
      if (independent_consumer && flight.valid()) {
        // Send the next real frame's already recorded work before waiting for
        // the single mailbox. No consumer dependency is placed on this stream.
        scheduler.Flush();
      }
      JoinFlight(); // Complete the previous output copy/ACK before single-mailbox reuse.
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
      std::shared_ptr<DlssgRawOutput> raw;
      std::vector<std::shared_ptr<DlssgRawOutput>> additional;
      if (independent_consumer) {
        raw = ClaimOutput(scene.color_format);
        if (!raw) return false; // Preserve guides=true for actual completion before reuse.
        for(unsigned i=1;i<generated_count;++i){auto next=ClaimOutput(scene.color_format);if(!next)return false;additional.push_back(std::move(next));}
      }
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
      scheduler.Flush();
      const auto flushed = Clock::now();
      if (!async_exchange) scheduler.Wait(last_recorded_tick);
      const auto completed = Clock::now();
      if (!async_exchange) {
        Observe();
        WaitConsumedAck();
      }
      if (!async_exchange) timing.ReadInput(DlssgNativeTiming::Ms(started, recorded),
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
      last_request = J{{"generated_count",generated_count},{"schema", DlssgGpu::Schema},
                       {"session", peer->session},
                       {"sequence", sequence + 1},
                       {"epoch", epoch},
                       {"source_game_frame", scene.source_game_frame},
                       {"previous_source_game_frame", last_source},
                       {"reset", reset},
                       {"display", display},
                       {"wire_order", "game-scene-bytes"},
                       {"transport", "gpu-dmabuf-v1"},
                       {"native_input_tick", last_recorded_tick},
                       {"native_input_GPU_complete", true},
                       {"camera", current_camera.constants}};
      if (independent_consumer) {
        last_request["native_completion_mode"] = "split-v1";
        last_request["native_input_domain"] = "draw";
      }
      if (async_exchange) {
        // The request is sent only after the exact submitted producer tick.
        // Native consumption is submitted only after validated Helper fence/release.
        const double record_ms = DlssgNativeTiming::Ms(started, recorded);
        const double flush_ms = DlssgNativeTiming::Ms(recorded, flushed);
        flight_eligible = std::make_shared<std::atomic<bool>>(false);
        auto promise = std::make_shared<std::promise<J>>();
        flight = promise->get_future().share();
        auto prior_ack = std::move(consumed_ack);
        std::shared_ptr<DlssgOutputTicket> ticket;
        if (independent_consumer) {
          ticket = std::make_shared<DlssgOutputTicket>();
          ticket->raw = raw; ticket->additional=std::move(additional);ticket->generated_count=generated_count; ticket->ready = flight; ticket->eligible = flight_eligible;
          ticket->epoch=epoch;ticket->source = scene.source_game_frame; ticket->previous_source = last_source;
          ticket->display = display;
          ticket->guide_record_cpu_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timing.guides.time_since_epoch()).count();
          ticket->producer_submit_cpu_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(flushed.time_since_epoch()).count();
          output_ticket = ticket;
        }
        auto job = [this, request = last_request, serial = helper_serial,
                        input_tick = last_recorded_tick,
                        producer = scheduler.GetWorkSemaphore(),
                        eligible = flight_eligible, promise, ticket,
                        prior_ack = std::move(prior_ack),
                        record_ms, flush_ms,
                        submitted = flushed,
                        previous_output = last_source,
                        pipeline = independent_consumer ? (instance.GetPresentQueue()!=instance.GetGraphicsQueue()?"consumer-queue-v1":"consumer-ready-v1") : late_exchange ? "late-ready-v1" : "submit-ready-v1"]() mutable {
          try {
            const auto wait_begin = Clock::now();
            producer->Wait(input_tick); // Never Scheduler::Wait/Flush on this worker.
            const auto input_complete = Clock::now();
            if (prior_ack.valid()) {
              Need(prior_ack.wait_for(std::chrono::seconds(30)) == std::future_status::ready,
                   "GPU submission prerequisite prior consumer ACK deadline");
              prior_ack.get();
            }
            const auto source = DlssgGpu::U(request, "source_game_frame");
            timing.ReadAsyncInput(source, independent_consumer ? 0 : previous_output, record_ms, flush_ms,
                                 DlssgNativeTiming::Ms(wait_begin, input_complete), pipeline);
            const auto exchange_begin = Clock::now();
            auto result = peer->Exchange(request, serial);
            const bool valid = DlssgGpu::B(result, "eligible");
            Need(!valid || (DlssgGpu::U(result, "result") == 1 &&
                           (DlssgGpu::U(result, "disable_word") & 255) == 0),
                 "GPU submission prerequisite model output eligibility");
            eligible->store(valid, std::memory_order_release);
            if (std::getenv("BB_FG_PROFILE") && DlssgGpu::U(request, "sequence") % 31 == 0) {
              auto profile = result; profile.erase("camera");
              profile["native_wait_cpu_ms"] = DlssgNativeTiming::Ms(exchange_begin, Clock::now());
              profile["producer_flush_cpu_ms"] = flush_ms;
              profile["producer_completion_wait_cpu_ms"] = DlssgNativeTiming::Ms(wait_begin, input_complete);
              profile["worker_start_delay_cpu_ms"] = DlssgNativeTiming::Ms(submitted, wait_begin);
              profile["producer_to_helper_ready_cpu_ms"] = DlssgNativeTiming::Ms(submitted, Clock::now());
              profile["pipeline"] = pipeline;
              std::printf("DLSSG_GPU_BRIDGE_PROFILE %s\n", profile.dump().c_str());
            }
            if (ticket) {
              const auto tick = ConsumeIndependent(*ticket);
              DlssgGpu::Peer::PublishConsumed(peer->root, request,
                                             DlssgGpu::U(result,"helper_serial"), tick);
              result["native_consumer_tick"] = tick;
              result["native_consumer_GPU_complete"] = true;
            }
            // Independent tickets become ready only after the real consumer
            // completion and ACK, even if never attached to a Present frame.
            promise->set_value(std::move(result));
          } catch (const std::exception &e) {
            // A submitted-tick prerequisite cannot be faked on error: fail-stop
            // with HOLD, never an exception stranded behind Finish().
            Fail(e);
          } catch (...) {
            Fail(std::runtime_error("GPU submission prerequisite worker unknown failure"));
          }
        };
        if (late_exchange) {
          Need(!deferred_job.valid(), "one dormant GPU request");
          deferred_job = std::packaged_task<void()>(std::move(job));
        } else worker->Submit(std::move(job));
        guides = false;
        pending = !independent_consumer;
        if (independent_consumer) {
          sequence = DlssgGpu::U(last_request,"sequence");
          last_source = scene.source_game_frame;
          previous_camera = std::move(current_camera);
          last_completed = Clock::now();
        }
        return true; // Provisional output; actual eligibility gates Present separately.
      }
      const auto wait_start = Clock::now();
      response = peer->Exchange(last_request, helper_serial);
      helper_serial = DlssgGpu::U(response, "helper_serial");
      pending = true;
      if (std::getenv("BB_FG_PROFILE") && (sequence + 1) % 31 == 0) {
        auto profile = response;
        profile.erase("camera");
        profile["native_wait_cpu_ms"] =
            DlssgNativeTiming::Ms(wait_start, Clock::now());
        profile["producer_finish_cpu_ms"] =
            DlssgNativeTiming::Ms(recorded, completed);
        profile["producer_flush_cpu_ms"] = DlssgNativeTiming::Ms(recorded, flushed);
        profile["producer_completion_wait_cpu_ms"] = DlssgNativeTiming::Ms(flushed, completed);
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
  void RecordOutput(Scheduler& stream, vk::Image target,
                    const std::shared_ptr<std::atomic<bool>>& recorded, unsigned output_index=0) {
      stream.Record([this, target, recorded, output_index](vk::CommandBuffer cmd) {
        if(output_index==0)timing.Mark(cmd, 4);
        mailbox->Ownership(VkCommandBuffer(cmd), 3+output_index, true, false);
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
          cmd.copyImage(vk::Image(mailbox->images[3+output_index].image),
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
        mailbox->Ownership(VkCommandBuffer(cmd), 3+output_index, false, false);
        if(output_index+1==generated_count)timing.Mark(cmd, 5);
        if (recorded)
          recorded->store(true, std::memory_order_release);
      });
  }
  void Consume(vk::Image target,
               const std::shared_ptr<std::atomic<bool>> &recorded = {}) {
    try {
      Need(pending, "GPU response before Native consume");
      // A late request starts only together with its guaranteed consume/discard.
      // A missing flip can leave this dormant indefinitely without stranding
      // a Helper output or starting the consumed-ACK deadline.
      if (deferred_job.valid()) worker->Submit(std::move(deferred_job));
      scheduler.EndRendering();
      last_recorded_tick = scheduler.CurrentTick();
      mailbox->MarkUse();
      const auto tick = last_recorded_tick;
      if (!async_exchange) {
        timing.upload_source = DlssgGpu::U(last_request, "source_game_frame");
        timing.upload_pending = bool(timing.pool);
      }
      RecordOutput(scheduler, target, recorded);
      Need(!consumed_ack.valid(), "one asynchronous consumer ACK");
      auto completed = std::make_shared<std::promise<double>>();
      consumed_ack = completed->get_future();
      consumed_tick = tick;
      // Existing completion thread waits the actual current tick, including
      // output release. Own every captured value: pause/FG-off must not delay
      // ACK, and no callback borrows us.
      scheduler.DeferPriorityOperation(
          [directory = peer->root, request = last_request,
           serial = helper_serial, result = flight, tick, completed] {
            try {
              const auto start = Clock::now();
              const auto completed_serial = result.valid()
                  ? DlssgGpu::U(result.get(), "helper_serial") : serial;
              DlssgGpu::Peer::PublishConsumed(directory, request, completed_serial, tick);
              completed->set_value(DlssgNativeTiming::Ms(start, Clock::now()));
            } catch (...) {
              completed->set_exception(std::current_exception());
            }
          });
      const auto submit_start = Clock::now();
      SubmitInfo submit{};
      if (async_exchange) submit.await_ready = [result = flight, request = last_request] {
        try {
          const auto start = Clock::now();
          Need(result.wait_for(std::chrono::seconds(30)) == std::future_status::ready,
               "GPU output submission prerequisite deadline");
          (void)result.get(); // Actual Helper fence/release validated by worker.
          if (std::getenv("BB_FG_PROFILE") && DlssgGpu::U(request, "sequence") % 31 == 0)
            std::printf("DLSSG_GPU_SUBMIT_READY_PROFILE source=%llu wait_cpu_ms=%.3f actual_Helper_GPU_complete=1 queue_lock_held=0\n",
                        (unsigned long long)DlssgGpu::U(request, "source_game_frame"),
                        DlssgNativeTiming::Ms(start, Clock::now()));
        } catch (const std::exception& e) { Fail(e); }
        catch (...) { Fail(std::runtime_error("unknown submission prerequisite failure")); }
      };
      scheduler.Flush(submit); // SAME draw timeline; prerequisite waits on the submit
                              // worker, outside submit_mutex, before QueueSubmit.
      const auto submitted = Clock::now();
      sequence = DlssgGpu::U(last_request, "sequence");
      last_source = DlssgGpu::U(last_request, "source_game_frame");
      previous_camera = std::move(current_camera);
      last_completed = submitted;
      pending = false;
      if (std::getenv("BB_FG_PROFILE") && sequence % 31 == 0)
        std::printf("DLSSG_GPU_CONSUMER_SUBMIT_PROFILE sequence=%llu "
                    "source=%llu flush_cpu_ms=%.3f explicit_GPU_wait=0\n",
                    (unsigned long long)sequence,
                    (unsigned long long)last_source,
                    DlssgNativeTiming::Ms(submit_start, submitted));
      if (sequence <= 3 || sequence % 31 == 0) {
        if (async_exchange)
          std::printf("DLSSG_GPU_NATIVE_QUEUED sequence=%llu source=%llu epoch=%llu "
                      "output_candidate=%u Native_copy_tick=%llu eligibility=pending "
                      "ACK_after_actual_GPU=async\n",
                      (unsigned long long)sequence, (unsigned long long)last_source,
                      (unsigned long long)epoch, unsigned(bool(target)), (unsigned long long)tick);
        else std::printf("DLSSG_GPU_NATIVE_OUTPUT sequence=%llu source=%llu "
                    "epoch=%llu eligible=%u Native_copy_tick=%llu "
                    "helper_serial=%llu pixel_host_transfer_bytes=0 ACK_after_actual_GPU=async\n",
                    (unsigned long long)sequence,
                    (unsigned long long)last_source, (unsigned long long)epoch,
                    unsigned(bool(target)), (unsigned long long)tick,
                    (unsigned long long)helper_serial);
      }
    } catch (const std::exception &e) {
      Fail(e);
    }
  }
  u64 ConsumeIndependent(DlssgOutputTicket& ticket) {
    if (!consumer_scheduler) consumer_scheduler = std::make_unique<Scheduler>(instance, false, instance.GetPresentQueue());
    auto& stream = *consumer_scheduler;
    const auto target = vk::Image(ticket.raw->image);
    const auto recorded = ticket.recorded;
    const auto tick = stream.CurrentTick();
    const auto copy_begin = Clock::now();
    Need(ticket.additional.size()+1==ticket.generated_count,"complete MFG raw bundle");
    RecordOutput(stream, target, recorded);
    for(unsigned i=1;i<ticket.generated_count;++i)RecordOutput(stream,vk::Image(ticket.additional[i-1]->image),{},i);
    stream.Flush();
    const auto copy_submitted = Clock::now();
    stream.Wait(tick); // Service thread, never the draw recording owner.
    consumer_completed_tick.store(tick, std::memory_order_release);
    if (timing.pool && ticket.source % 31 == 0) {
      u64 values[2]{};
      Check(instance.GetDevice().getQueryPoolResults(*timing.pool,4,2,sizeof values,values,sizeof(u64),vk::QueryResultFlagBits::e64));
      std::printf("DLSSG_INDEPENDENT_COPY source=%llu consumer_tick=%llu GPU_ms=%.3f record_submit_cpu_ms=%.3f completion_wait_cpu_ms=%.3f actual_GPU_complete=1\n",
          (unsigned long long)ticket.source,(unsigned long long)tick,timing.Delta(values[0],values[1]),DlssgNativeTiming::Ms(copy_begin,copy_submitted),DlssgNativeTiming::Ms(copy_submitted,Clock::now()));
    }
    return tick;
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
      JoinFlight();
      WaitConsumedAck();
      if (independent_consumer) {
        auto done = std::make_shared<std::promise<void>>(); auto waited = done->get_future();
        worker->Submit([this,done] {
          try { if (consumer_scheduler) { consumer_scheduler->Finish(); consumer_scheduler.reset(); } done->set_value(); }
          catch (const std::exception& e) { Fail(e); }
        });
        Need(waited.wait_for(std::chrono::seconds(30)) == std::future_status::ready,"consumer drain deadline");
        waited.get();
      }
      worker.reset();
      peer->Retire(*mailbox, last_request, helper_serial,
                   independent_consumer ? consumer_completed_tick.load(std::memory_order_acquire) : 0);
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
