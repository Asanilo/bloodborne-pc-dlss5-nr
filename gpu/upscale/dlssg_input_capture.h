// SPDX-License-Identifier: GPL-2.0-or-later
// Finite input diagnostic at existing FG hooks. No FSR or DLSSG dispatch/Present.
#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include "buffers/buffer.h"
#include "render/scheduler.h"
#include "upscale/vk_frame_generation.h"
#include "vulkan/instance.h"

namespace Vulkan {
class DlssgInputCapture {
    const Instance& instance;
    Scheduler& scheduler;
    std::string directory, trigger;
    std::unique_ptr<VideoCore::Buffer> depth, motion, color;
    FrameGeneration::Scene pending{};
    unsigned count = 0;
    bool armed = false, have_guides = false, continuous = false;
    u64 readback_tick = 0;
    u64 previous_source = 0;

    void Copy(vk::Image image, VideoCore::Buffer& buffer, u32 w, u32 h,
              vk::ImageAspectFlagBits aspect) {
        scheduler.EndRendering();
        scheduler.Record([image, target = buffer.Handle(), w, h, aspect](vk::CommandBuffer cmd) {
            const vk::ImageMemoryBarrier2 before{
                .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
                .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image, .subresourceRange = {aspect, 0, 1, 0, 1},
            };
            cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
            const vk::BufferImageCopy copy{
                .imageSubresource = {aspect, 0, 0, 1}, .imageExtent = {w, h, 1},
            };
            cmd.copyImageToBuffer(image, vk::ImageLayout::eGeneral, target, copy);
            auto after = before;
            after.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            after.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
            after.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            after.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            const vk::BufferMemoryBarrier2 host{
                .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                .dstAccessMask = vk::AccessFlagBits2::eHostRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = target, .size = VK_WHOLE_SIZE,
            };
            cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &host,
                                  .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
        });
    }

    void Write(const std::string& name, VideoCore::Buffer& buffer) {
        buffer.Invalidate(0, buffer.SizeBytes());
        const std::string path = directory + "/frame-" + std::to_string(count) + "-" + name;
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) throw std::runtime_error("DLSSG capture file open failed");
        const bool written = std::fwrite(buffer.mapped_data.data(), 1, buffer.SizeBytes(), f) == buffer.SizeBytes();
        if (std::fclose(f) != 0 || !written) throw std::runtime_error("DLSSG capture file write failed");
    }

public:
    DlssgInputCapture(const Instance& i, Scheduler& s, bool live=false) : instance(i), scheduler(s), continuous(live) {
        const char* d = std::getenv(live?"BB_DLSSG_STREAM_DIR":"BB_DLSSG_CAPTURE_DIR");
        const char* t = std::getenv("BB_DLSSG_CAPTURE_TRIGGER");
        if (!d || !*d || (!live && (!t || !*t))) throw std::runtime_error("DLSSG capture requires private paths");
        directory = d; if(t)trigger=t;armed=live;
    }
    // The FrameGeneration owner drains the scheduler before destroying these buffers.
    bool Guides(const FrameGeneration::Scene& in) {
        if (!continuous && count >= 6) return false;
        if (!armed) {
            if (!std::filesystem::exists(trigger) || !std::filesystem::remove(trigger)) return false;
            std::filesystem::create_directory(directory);
            armed = true;
        }
        if (in.width != 1920 || in.height != 1080 || in.render_width != 1280 ||
            in.render_height != 720 || !in.source_game_frame || !in.depth || !in.motion ||
            (in.depth_format != vk::Format::eD32Sfloat && in.depth_format != vk::Format::eD32SfloatS8Uint) ||
            (in.color_format != vk::Format::eR8G8B8A8Unorm && in.color_format != vk::Format::eB8G8R8A8Unorm)) {
            std::printf("DLSSG_CAPTURE_UNSUPPORTED fixed_1080q_input_required=1\n");
            return false;
        }
        if (!depth || have_guides) scheduler.Finish();
        if (!depth) {
            depth = std::make_unique<VideoCore::Buffer>(instance, 0, u64(1280)*720*4, VideoCore::MemoryType::HostCached, "DLSSG depth capture");
            motion = std::make_unique<VideoCore::Buffer>(instance, 0, u64(1280)*720*4, VideoCore::MemoryType::HostCached, "DLSSG MV capture");
            color = std::make_unique<VideoCore::Buffer>(instance, 0, u64(1920)*1080*4, VideoCore::MemoryType::HostCached, "DLSSG color capture");
        }
        pending = in;
        Copy(in.depth, *depth, 1280, 720, vk::ImageAspectFlagBits::eDepth);
        Copy(in.motion, *motion, 1280, 720, vk::ImageAspectFlagBits::eColor);
        have_guides = true;
        return true;
    }
    bool ReadColor(const FrameGeneration::Scene& in, vk::Image image) {
        if (!have_guides || in.source_game_frame != pending.source_game_frame) return false;
        Copy(image, *color, 1920, 1080, vk::ImageAspectFlagBits::eColor);
        readback_tick = scheduler.CurrentTick();
        scheduler.Finish(); // Host reads never precede actual GPU completion.
        for(auto* buffer:{color.get(),depth.get(),motion.get()})buffer->Invalidate(0,buffer->SizeBytes());
        have_guides=false;
        return true;
    }
    std::span<const u8> ColorBytes()const{return color->mapped_data;}
    std::span<const u8> DepthBytes()const{return depth->mapped_data;}
    std::span<const u8> MotionBytes()const{return motion->mapped_data;}
    void Color(const FrameGeneration::Scene& in, vk::Image image) {
        if(!ReadColor(in,image))return;
        const auto tick=readback_tick;
        Write("color.raw", *color); Write("depth.raw", *depth); Write("motion.raw", *motion);
        using J = nlohmann::json;
        const auto vector = [](const float (&v)[3]) { return J::array({v[0],v[1],v[2]}); };
        J metadata{{"schema","dlssg-bloodborne-input-v1"},{"index",count},
            {"source_game_frame",in.source_game_frame},{"previous_source_game_frame",previous_source},
            {"reset",in.reset || !previous_source || in.source_game_frame != previous_source+1},
            {"width",in.width},{"height",in.height},{"render_width",in.render_width},{"render_height",in.render_height},
            {"color_format",u32(in.color_format)},{"depth_encoding","d32_float"},{"motion_encoding","rg16f_pixels_previous_minus_current"},
            {"jitter",{in.jitter_x,in.jitter_y}},{"frame_ms",in.frame_ms},
            {"near",in.camera_near},{"far",in.camera_far},{"vertical_fov",in.vertical_fov},
            {"camera_position",vector(in.camera_position)},{"camera_up",vector(in.camera_up)},
            {"camera_right",vector(in.camera_right)},{"camera_forward",vector(in.camera_forward)},
            {"actual_GPU_complete",true},{"recorded_tick",tick},{"generated_frames",0},{"scope","finite input capture, no interpolation"}};
        const auto path = directory + "/frame-" + std::to_string(count) + ".json";
        const auto text = metadata.dump(2) + "\n";
        FILE* f = std::fopen((path+".tmp").c_str(),"wb");
        if (!f) throw std::runtime_error("DLSSG metadata open failed");
        const bool written = std::fwrite(text.data(),1,text.size(),f)==text.size();
        if (std::fclose(f)!=0 || !written || std::rename((path+".tmp").c_str(),path.c_str())!=0)
            throw std::runtime_error("DLSSG metadata publish failed");
        std::printf("DLSSG_INPUT_CAPTURE index=%u source_game_frame=%llu actual_GPU_complete=1 recorded_tick=%llu generated_frames=0\n",count,(unsigned long long)in.source_game_frame,(unsigned long long)tick);
        previous_source = in.source_game_frame;
        ++count; have_guides = false;
    }
};
} // namespace Vulkan
