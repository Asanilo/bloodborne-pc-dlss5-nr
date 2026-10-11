// SPDX-License-Identifier: GPL-2.0-or-later
#include "c3_game_bridge.h"

#include <cerrno>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <set>
#include <cmath>
#include <unordered_set>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include "video_core/renderer_vulkan/c3_nr_process.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/host_shaders/c3_game_bridge_comp.h"
#include "video_core/host_shaders/c3_frame_validate_comp.h"
#include "video_core/host_shaders/c3_frame_validate_return_comp.h"

namespace Vulkan {
namespace {
using Json = nlohmann::json;
constexpr vk::ExternalMemoryHandleTypeFlagBits HandleType =
    vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT;
constexpr size_t MaxControlBytes = 64 * 1024;
constexpr std::string_view ProfileId = "hdr-run-dlaa-1to1-j0-manual-wp1-v1";
constexpr vk::ImageUsageFlags SharedUsage =
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

std::string Read(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > MaxControlBytes)
        throw std::runtime_error("C3 control exceeds 64 KiB: " + path);
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), {}) : std::string{};
}

void AtomicWrite(const std::string& path, const std::string& value) {
    const auto temporary = path + ".tmp-" + std::to_string(getpid());
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("C3 open temp control: " + temporary);
    output << value << '\n';
    output.flush();
    if (!output) throw std::runtime_error("C3 write temp control: " + temporary);
    output.close();
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        throw std::runtime_error("C3 publish control: " + std::string(std::strerror(errno)));
    }
}

std::vector<C3FdIdentity> SnapshotFds(pid_t pid, const std::string& output_path) {
    const auto root = std::filesystem::path("/proc") / std::to_string(pid) / "fd";
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("C3 fd inventory unavailable for pid=" + std::to_string(pid));
    std::vector<C3FdIdentity> records;
    Json inventory = Json::array();
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        char* end = nullptr;
        const long number = std::strtol(name.c_str(), &end, 10);
        if (!end || *end || number < 0 || number > INT32_MAX) continue;
        std::array<char, 4096> target{};
        const auto link = entry.path().string();
        const ssize_t size = readlink(link.c_str(), target.data(), target.size() - 1);
        struct stat st{};
        C3FdIdentity record;
        record.number = static_cast<int>(number);
        record.target = size < 0 ? "<readlink-error:" + std::to_string(errno) + ">"
                                 : std::string(target.data(), static_cast<size_t>(size));
        record.stat_ok = stat(link.c_str(), &st) == 0;
        if (record.stat_ok) {
            record.device = static_cast<uint64_t>(st.st_dev);
            record.inode = static_cast<uint64_t>(st.st_ino);
        }
        inventory.push_back({{"fd", record.number}, {"target", record.target},
                             {"stat_ok", record.stat_ok}, {"device", record.device},
                             {"inode", record.inode}});
        records.push_back(std::move(record));
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.number < b.number;
    });
    AtomicWrite(output_path, inventory.dump());
    return records;
}

bool SamePayload(const C3FdIdentity& a, const C3FdIdentity& b) {
    return a.stat_ok && b.stat_ok && a.target == b.target && a.device == b.device &&
           a.inode == b.inode;
}

bool SameInventoryEntry(const C3FdIdentity& a, const C3FdIdentity& b) {
    return a.number == b.number && a.target == b.target && a.stat_ok == b.stat_ok &&
           a.device == b.device && a.inode == b.inode;
}

Json FdInventoryJson(const std::vector<C3FdIdentity>& records) {
    Json result = Json::array();
    for (const auto& record : records)
        result.push_back({{"fd", record.number}, {"target", record.target},
                          {"stat_ok", record.stat_ok}, {"device", record.device},
                          {"inode", record.inode}});
    return result;
}

bool HasFile(const std::string& path) {
    return std::filesystem::is_regular_file(path);
}

Json StrictJson(std::string_view text, std::string_view source) {
    if (text.empty() || text.size() > MaxControlBytes)
        throw std::runtime_error("C3 JSON empty or exceeds 64 KiB: " + std::string(source));
    std::unordered_set<std::string> top_keys;
    bool duplicate = false;
    Json::parser_callback_t callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
        // nlohmann reports root object members at depth 1. Reject duplicate top-level keys
        // before they can be silently replaced by the DOM parser.
        if (depth == 1 && event == Json::parse_event_t::key) {
            const auto key = parsed.get<std::string>();
            if (!top_keys.insert(key).second) duplicate = true;
        }
        return true;
    };
    Json result = Json::parse(text.begin(), text.end(), callback, true, false);
    if (duplicate || !result.is_object())
        throw std::runtime_error("C3 JSON duplicate top-level key or non-object: " +
                                 std::string(source));
    return result;
}

std::optional<Json> TryReadJson(const std::string& path) {
    if (!HasFile(path)) return std::nullopt;
    return StrictJson(Read(path), path);
}

std::string WaitJson(const std::string& path, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto contents = Read(path);
        if (!contents.empty() && std::chrono::steady_clock::now() < deadline) return contents;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return {};
}

int DuplicateRemoteFd(pid_t exporter, int remote_fd) {
    if (exporter <= 1 || remote_fd < 0) throw std::runtime_error("C3 invalid exporter fd");
    const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, exporter, 0));
    if (pidfd < 0) throw std::runtime_error("C3 pidfd_open errno=" + std::to_string(errno));
    const int duplicate = static_cast<int>(syscall(SYS_pidfd_getfd, pidfd, remote_fd, 0));
    const int error = errno;
    close(pidfd);
    if (duplicate < 0) throw std::runtime_error("C3 pidfd_getfd errno=" + std::to_string(error));
    return duplicate;
}

uint32_t ExpectedFormat(std::string_view role) {
    if (role == "color" || role == "output") return 109; // R32G32B32A32_SFLOAT
    if (role == "depth") return 100;                     // R32_SFLOAT
    if (role == "mv") return 103;                        // R32G32_SFLOAT
    return 0;
}

Json Envelope(const std::string& session, std::string_view mode, std::string_view op, std::string_view status,
              u32 frame, u64 source_frame) {
    return Json{{"session", session}, {"mode", mode}, {"generation", 0}, {"frame", frame},
                {"resource_set", session + "-g0"}, {"source_game_frame", source_frame},
                {"profile_id", ProfileId}, {"width", 1920}, {"height", 1080},
                {"render_width", 1920}, {"render_height", 1080}, {"op", op},
                {"status", status}, {"completed_GPU", true}, {"output_valid", false},
                {"model_valid", false}};
}

uint64_t RequireUnsigned(const Json& value, std::string_view key) {
    const std::string field(key);
    if (!value.is_object() || !value.contains(field) || !value.at(field).is_number_unsigned())
        throw std::runtime_error("C3 unsigned JSON field required: " + field);
    return value.at(field).get<uint64_t>();
}
std::string RequireString(const Json& value, std::string_view key) {
    const std::string field(key);
    if (!value.is_object() || !value.contains(field) || !value.at(field).is_string())
        throw std::runtime_error("C3 string JSON field required: " + field);
    return value.at(field).get<std::string>();
}
bool RequireBool(const Json& value, std::string_view key) {
    const std::string field(key);
    if (!value.is_object() || !value.contains(field) || !value.at(field).is_boolean())
        throw std::runtime_error("C3 boolean JSON field required: " + field);
    return value.at(field).get<bool>();
}

void ValidateEnvelope(const Json& value, const std::string& session, std::string_view mode,
                      std::string_view op, std::string_view status, u32 frame, u64 source_frame,
                      bool output_valid = false, bool model_valid = false) {
    if (RequireString(value, "session") != session || RequireString(value, "mode") != mode ||
        RequireUnsigned(value, "generation") != 0 || RequireUnsigned(value, "frame") != frame ||
        RequireString(value, "resource_set") != session + "-g0" ||
        RequireUnsigned(value, "source_game_frame") != source_frame ||
        RequireString(value, "profile_id") != ProfileId ||
        RequireUnsigned(value, "width") != 1920 || RequireUnsigned(value, "height") != 1080 ||
        RequireUnsigned(value, "render_width") != 1920 ||
        RequireUnsigned(value, "render_height") != 1080 || RequireString(value, "op") != op ||
        RequireString(value, "status") != status || !RequireBool(value, "completed_GPU") ||
        RequireBool(value, "output_valid") != output_valid ||
        RequireBool(value, "model_valid") != model_valid)
        throw std::runtime_error("C3 strict control tuple/status mismatch");
}

vk::ImageView FindView(const std::vector<C3ImportedImage>& images, std::string_view role) {
    for (const auto& image : images) if (image.role == role) return image.view;
    return {};
}

C3ImportedImage& FindImage(std::vector<C3ImportedImage>& images, std::string_view role) {
    for (auto& image : images) if (image.role == role) return image;
    throw std::runtime_error("C3 image role absent: " + std::string(role));
}

uint64_t Fnv1a(const uint8_t* bytes, size_t size) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    return hash;
}

std::string Hex64(uint64_t value) {
    char text[17]{};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

bool JsonVkBool32(const Json& value, std::string_view field) {
    if (value.is_boolean()) return value.get<bool>();
    if (!value.is_number_unsigned() && !value.is_number_integer())
        throw std::runtime_error("C3 metadata VkBool32 has invalid JSON type: " +
                                 std::string(field));
    const auto integer = value.get<int64_t>();
    if (integer != 0 && integer != 1)
        throw std::runtime_error("C3 metadata VkBool32 is outside 0/1: " +
                                 std::string(field));
    return integer == 1;
}

uint64_t JsonUnsigned(const Json& value, std::string_view field, uint64_t maximum = UINT64_MAX) {
    if (!value.is_number_unsigned() && !value.is_number_integer())
        throw std::runtime_error("C3 metadata integer has invalid JSON type: " +
                                 std::string(field));
    if (value.is_number_integer() && value.get<int64_t>() < 0)
        throw std::runtime_error("C3 metadata integer is negative: " + std::string(field));
    const uint64_t result = value.get<uint64_t>();
    if (result > maximum)
        throw std::runtime_error("C3 metadata integer out of range: " + std::string(field));
    return result;
}

bool WaitSubmitted(Scheduler& scheduler, u64 tick) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (scheduler.IsFree(tick)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return scheduler.IsFree(tick);
}
} // namespace

void C3GameBridge::Quarantine(std::string_view reason) {
    quarantined = true;
    const std::string message = "C3_QUARANTINE reason=" + std::string(reason) +
        " resources_retained=" + std::to_string(resources.size()) +
        " last_submitted_tick=" + std::to_string(last_submitted_tick) +
        " last_completed_tick=" + std::to_string(last_completed_tick) +
        " outstanding_tick=" + std::to_string(outstanding_tick) + "\n";
    std::fprintf(stderr, "%s", message.c_str());
    if (process_owner) {
        std::fprintf(stderr,
            "C3_PE_STATE child_pid=%d exporter_pid=%d completed=%u exit_status=%d identity_current=%u\n",
            process_owner->ChildPid(), process_owner->ExporterPid(), process_owner->Completed(),
            process_owner->ExitStatus(), process_owner->ExporterIdentityCurrent());
    }
    if (!run_dir.empty() && !abort_sent) {
        try {
            AtomicWrite(run_dir + "/native.abort", message);
            abort_sent = true;
            std::fprintf(stderr, "C3_HELPER_ABORT published=1 exporter_pid=%d\n", exporter_pid);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "C3_HELPER_ABORT published=0 reason=%s\n", e.what());
        }
    }
}

void C3GameBridge::CreateLocalReturnAndPipeline(const Instance& instance) {
    local_return = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
    local_return.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    local_return_view = instance.GetDevice().createImageViewUnique({
        .image = vk::Image(local_return), .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    }).value;
    std::array<vk::DescriptorSetLayoutBinding, 8> bindings{};
    for (u32 i = 0; i < bindings.size(); ++i) {
        bindings[i] = {.binding = i,
                       .descriptorType = i == 1 ? vk::DescriptorType::eCombinedImageSampler
                                                : vk::DescriptorType::eStorageImage,
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eCompute};
    }
    descriptor_layout = instance.GetDevice().createDescriptorSetLayoutUnique({
        .bindingCount = static_cast<u32>(bindings.size()), .pBindings = bindings.data(),
    }).value;
    const vk::PushConstantRange push{.stageFlags = vk::ShaderStageFlagBits::eCompute,
                                     .offset = 0, .size = sizeof(u32)};
    pipeline_layout = instance.GetDevice().createPipelineLayoutUnique({
        .setLayoutCount = 1, .pSetLayouts = &*descriptor_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    }).value;
    const auto module = CompileSPV(C3_GAME_BRIDGE_COMP, instance.GetDevice());
    pipeline = instance.GetDevice().createComputePipelineUnique({}, {
        .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
        .layout = *pipeline_layout,
    }).value;
    instance.GetDevice().destroyShaderModule(module);
    const std::array<vk::DescriptorPoolSize, 2> pool_sizes{{
        {vk::DescriptorType::eStorageImage, 7},
        {vk::DescriptorType::eCombinedImageSampler, 1},
    }};
    descriptor_pool = instance.GetDevice().createDescriptorPoolUnique({
        .maxSets = 1, .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    }).value;
    descriptor_set = instance.GetDevice().allocateDescriptorSets({
        .descriptorPool = *descriptor_pool, .descriptorSetCount = 1,
        .pSetLayouts = &*descriptor_layout,
    }).value.front();
    depth_sampler = instance.GetDevice().createSamplerUnique({
        .magFilter = vk::Filter::eNearest, .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge, .maxLod = 0.0f,
    }).value;
    const u64 pixels = static_cast<u64>(width) * height;
    readback = std::make_unique<VideoCore::Buffer>(
        instance, 0, pixels * 40, VideoCore::MemoryType::HostCached,
        "C3 scheduled full audit readback");
    if (readback->mapped_data.size() < pixels * 40)
        throw std::runtime_error("C3 host readback buffer is not mapped to requested size");
    validation_stats = std::make_unique<VideoCore::Buffer>(
        instance, 0, sizeof(C3FrameValidationStats), VideoCore::MemoryType::HostCached,
        "C3 per-frame 64B validation");
    if (validation_stats->mapped_data.size() < sizeof(C3FrameValidationStats))
        throw std::runtime_error("C3 validation readback is not mapped");
    return_validation_stats = std::make_unique<VideoCore::Buffer>(
        instance, 0, 16, VideoCore::MemoryType::HostCached, "C3 native-only return-half validation");
    if (return_validation_stats->mapped_data.size() < 16)
        throw std::runtime_error("C3 return-half counter is not mapped");
    const std::array<vk::DescriptorSetLayoutBinding, 5> validation_bindings{{
        {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        {4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    const auto device = instance.GetDevice();
    validation_descriptor_layout = device.createDescriptorSetLayoutUnique({
        .bindingCount = 5, .pBindings = validation_bindings.data()}).value;
    const vk::PushConstantRange validation_push{vk::ShaderStageFlagBits::eCompute, 0, 8};
    validation_pipeline_layout = device.createPipelineLayoutUnique({
        .setLayoutCount = 1, .pSetLayouts = &*validation_descriptor_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &validation_push}).value;
    const auto validation_module = CompileSPV(C3_FRAME_VALIDATE_COMP, device);
    validation_pipeline = device.createComputePipelineUnique({}, {
        .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                  .module = validation_module, .pName = "main"},
        .layout = *validation_pipeline_layout}).value;
    device.destroyShaderModule(validation_module);
    const auto return_module = CompileSPV(C3_FRAME_VALIDATE_RETURN_COMP, device);
    validation_return_pipeline = device.createComputePipelineUnique({}, {
        .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = return_module, .pName = "main"},
        .layout = *validation_pipeline_layout}).value;
    device.destroyShaderModule(return_module);
    const std::array<vk::DescriptorPoolSize, 2> validation_pool_sizes{{
        {vk::DescriptorType::eStorageImage, 3}, {vk::DescriptorType::eStorageBuffer, 2}}};
    validation_descriptor_pool = device.createDescriptorPoolUnique({
        .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = validation_pool_sizes.data()}).value;
    validation_descriptor_set = device.allocateDescriptorSets({
        .descriptorPool = *validation_descriptor_pool, .descriptorSetCount = 1,
        .pSetLayouts = &*validation_descriptor_layout}).value.front();
}

void C3GameBridge::RecordValidation(const Instance& instance, Scheduler& scheduler,
                                   vk::ImageView source, vk::ImageView output, bool validate_return) {
    const auto cmd = scheduler.CommandBuffer();
    // Prior use of this single stats buffer was fenced before descriptor/counter reuse.
    cmd.fillBuffer(validation_stats->Handle(), 0, sizeof(C3FrameValidationStats), 0);
    if (validate_return) cmd.fillBuffer(return_validation_stats->Handle(), 0, 16, 0);
    const vk::BufferMemoryBarrier2 clear_to_compute{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = validation_stats->Handle(), .offset = 0, .size = sizeof(C3FrameValidationStats)};
    cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &clear_to_compute});
    if (validate_return) {
        auto clear_half = clear_to_compute;
        clear_half.buffer = return_validation_stats->Handle(); clear_half.size = 16;
        cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &clear_half});
    }
    const std::array<vk::DescriptorImageInfo, 3> infos{{
        {.imageView = source, .imageLayout = vk::ImageLayout::eGeneral},
        {.imageView = output, .imageLayout = vk::ImageLayout::eGeneral},
        {.imageView = *local_return_view, .imageLayout = vk::ImageLayout::eGeneral}}};
    const vk::DescriptorBufferInfo buffer_info{validation_stats->Handle(), 0,
                                              sizeof(C3FrameValidationStats)};
    const vk::DescriptorBufferInfo half_info{return_validation_stats->Handle(), 0, 16};
    const std::array<vk::WriteDescriptorSet, 5> writes{{
        {.dstSet = validation_descriptor_set, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &infos[0]},
        {.dstSet = validation_descriptor_set, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &infos[1]},
        {.dstSet = validation_descriptor_set, .dstBinding = 2, .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &buffer_info},
        {.dstSet = validation_descriptor_set, .dstBinding = 3, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &infos[2]},
        {.dstSet = validation_descriptor_set, .dstBinding = 4, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &half_info}}};
    instance.GetDevice().updateDescriptorSets(writes, {});
    const std::array<u32, 2> extent{width, height};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, validate_return ? *validation_return_pipeline : *validation_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *validation_pipeline_layout, 0,
                          validation_descriptor_set, {});
    cmd.pushConstants(*validation_pipeline_layout, vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(extent), extent.data());
    cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);
    const vk::BufferMemoryBarrier2 compute_to_host{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eHostRead,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = validation_stats->Handle(), .offset = 0, .size = sizeof(C3FrameValidationStats)};
    cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &compute_to_host});
    if (validate_return) {
        auto half_to_host = compute_to_host;
        half_to_host.buffer = return_validation_stats->Handle(); half_to_host.size = 16;
        cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &half_to_host});
    }
}

C3FrameValidationStats C3GameBridge::ReadValidation() {
    validation_stats->Invalidate(0, sizeof(C3FrameValidationStats));
    C3FrameValidationStats result{};
    std::memcpy(&result, validation_stats->mapped_data.data(), sizeof(result));
    if (result.raw_rgb_finite || result.raw_rgb_written || result.raw_rgb_nan ||
        result.reserved0 || result.reserved1 ||
        result.visited_pixels != static_cast<u64>(width) * height)
        throw std::runtime_error("C3 native 64B GPU layout/visited/raw-not-applicable mismatch");
    return result;
}

bool C3GameBridge::AuditFdRetired(const Instance& instance, const C3NrProcess& process) {
    try {
        const auto path = run_dir + "/fd-retired-" + session + "-g0.json";
        if (!HasFile(path)) return false;
        const Json fd_retired = StrictJson(Read(path), path);
        ValidateEnvelope(fd_retired, session, mode, "fd_retired", "FD_RETIRED", 0, 0);
        if (!fd_retired.at("closed_fd").is_array() || fd_retired.at("closed_fd").size() != 4 ||
            resources.size() != 4 || exporter_raw_fds.size() != 4)
            throw std::runtime_error("C3 fd-retired must map four imported role/fd pairs");
        std::set<std::string> closed_roles;
        for (const auto& item : fd_retired.at("closed_fd")) {
            if (!item.is_object() || !item.contains("role") || !item["role"].is_string() ||
                !item.contains("fd") || !item["fd"].is_number_unsigned())
                throw std::runtime_error("C3 fd-retired role/fd JSON type mismatch");
            const auto role = item.at("role").get<std::string>();
            const auto fd64 = item.at("fd").get<uint64_t>();
            if (fd64 > INT32_MAX) throw std::runtime_error("C3 fd-retired fd overflow");
            const auto fd = static_cast<int>(fd64);
            const auto resource = std::find_if(resources.begin(), resources.end(),
                [&](const auto& image) { return image.role == role && image.raw_fd == fd; });
            if (!closed_roles.insert(role).second || resource == resources.end())
                throw std::runtime_error("C3 fd-retired role does not match imported raw fd");
            const auto original = std::find_if(exporter_raw_fds.begin(), exporter_raw_fds.end(),
                [&](const auto& record) { return record.number == fd; });
            if (original == exporter_raw_fds.end() || !original->stat_ok)
                throw std::runtime_error("C3 original raw FD identity missing");
        }
        if (closed_roles != std::set<std::string>{"color", "depth", "mv", "output"})
            throw std::runtime_error("C3 fd-retired role set incomplete");
        const auto after = SnapshotFds(process.ExporterPid(),
            run_dir + "/wine-fd-active-retired.json");
        for (const auto& original : exporter_raw_fds) {
            if (std::any_of(after.begin(), after.end(), [&](const auto& record) {
                    return SamePayload(original, record);
                }))
                throw std::runtime_error("C3 original raw DMA_BUF payload remains after consume");
        }
        fd_retired_ready = true;
        CreateLocalReturnAndPipeline(instance);
        prepared = true;
        std::printf("C3_IMPORT_GATE PASS session=%s resources=4 fd_retired_role_payloads=4\n",
                    session.c_str());
        return true;
    } catch (const std::exception& e) {
        Quarantine(e.what());
        return false;
    }
}

bool C3GameBridge::Prepare(const Instance& instance, Scheduler& scheduler,
                           C3NrProcess& process, std::string_view requested_session,
                           const std::string& requested_run_dir,
                           const std::string& device_uuid, const std::string& driver_uuid,
                           u32 requested_width, u32 requested_height) {
    if (prepared || quarantined || finished) return prepared && !quarantined;
    if (requested_width != 1920 || requested_height != 1080) return false;
    process_owner = &process;
    if (!imported && (session.empty() || run_dir.empty())) {
        session = requested_session;
        mode = std::string(process.Mode());
        run_dir = requested_run_dir;
        frame_count = process.FrameCount();
        width = requested_width;
        height = requested_height;
    }
    if (process.SetupFailed()) {
        Quarantine("C3 helper identity/bootstrap worker failed: " + process.Failure());
        return false;
    }
    if (!process.SetupReady() || !process.ExporterIdentityCurrent()) return false;
    if (!cold_deadline_started) {
        cold_deadline_started = true;
        cold_started_at = std::chrono::steady_clock::now();
        control_deadline = cold_started_at + std::chrono::seconds(120);
        std::printf("C3_NATIVE_DEADLINE phase=cold_ready_ms limit=120000 start=verified_exporter_identity\n");
    }
    if (session != requested_session || run_dir != requested_run_dir || width != requested_width ||
        height != requested_height) {
        Quarantine("C3 session/profile changed while slot is active");
        return false;
    }
    exporter_pid = process.ExporterPid();
    if (imported) {
        if (!fd_retired_ready) {
            if (!HasFile(run_dir + "/fd-retired-" + session + "-g0.json")) {
                if (std::chrono::steady_clock::now() >= control_deadline)
                    Quarantine("C3 fd_retired control deadline 60000ms expired; imports retained");
                return false;
            }
            if (!AuditFdRetired(instance, process)) return false;
            return true;
        }
        return prepared;
    }
    const auto baseline_path = run_dir + "/baseline-" + session + "-g0.json";
    if (!HasFile(baseline_path)) {
        if (std::chrono::steady_clock::now() >= control_deadline)
            Quarantine("C3 cold model readiness deadline 120000ms expired before baseline");
        return false;
    }
    try {
        if (!baseline_audited) {
            const Json baseline = StrictJson(Read(baseline_path), baseline_path);
            ValidateEnvelope(baseline, session, mode, "baseline", "BASELINE", 0, 0);
            if (!baseline.contains("model_init") || !RequireBool(baseline, "model_init") ||
                !baseline.contains("model_feature_created") ||
                !RequireBool(baseline, "model_feature_created") ||
                RequireUnsigned(baseline, "cold_init_timeout_ms") != 120000 ||
                RequireUnsigned(baseline, "control_timeout_ms") != 60000 ||
                RequireUnsigned(baseline, "gpu_timeout_ms") != 10000 ||
                RequireUnsigned(baseline, "frame_completion_timeout_ms") != 30000)
                throw std::runtime_error("C3 baseline model readiness/budgets mismatch");
            const auto baseline_fds = SnapshotFds(getpid(),
                run_dir + "/native-fd-baseline.json");
            exporter_baseline_fds = SnapshotFds(process.ExporterPid(),
                run_dir + "/wine-fd-baseline.json");
            auto baseline_ack = Envelope(session, mode, "baseline_audit", "BASELINE_AUDITED", 0, 0);
            baseline_ack["baseline_fd_count"] = baseline_fds.size();
            baseline_ack["inventory"] = "native-fd-baseline.json";
            AtomicWrite(run_dir + "/native-baseline-" + session + "-g0.json",
                        baseline_ack.dump());
            baseline_audited = true;
            control_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            std::printf("C3_NATIVE_DEADLINE phase=metadata_control_ms limit=60000 start=baseline_ack\n");
            return false;
        }

        const auto metadata_path = run_dir + "/meta-g0.json";
        if (!HasFile(metadata_path)) {
            if (std::chrono::steady_clock::now() >= control_deadline)
                Quarantine("C3 metadata control deadline 60000ms expired");
            return false;
        }
        const Json metadata = StrictJson(Read(metadata_path), metadata_path);
        if (RequireString(metadata, "session") != session ||
            RequireString(metadata, "mode") != mode || RequireUnsigned(metadata, "generation") != 0 ||
            RequireString(metadata, "resource_set") != session + "-g0" ||
            RequireUnsigned(metadata, "linux_pid") != static_cast<uint64_t>(process.ExporterPid()) ||
            RequireUnsigned(metadata, "handle_type") != static_cast<uint32_t>(HandleType) ||
            RequireString(metadata, "op") != "metadata" || RequireString(metadata, "profile_id") != ProfileId ||
            RequireUnsigned(metadata, "frame") != 0 || RequireUnsigned(metadata, "source_game_frame") != 0 ||
            RequireBool(metadata, "output_valid") || RequireBool(metadata, "model_valid") ||
            RequireUnsigned(metadata, "max_frames") != frame_count ||
            RequireUnsigned(metadata, "frame_count") != frame_count ||
            RequireUnsigned(metadata, "history_version") != 1 ||
            RequireUnsigned(metadata, "params_version") != 1 ||
            RequireUnsigned(metadata, "cold_init_timeout_ms") != 120000 ||
            RequireUnsigned(metadata, "control_timeout_ms") != 60000 ||
            RequireUnsigned(metadata, "gpu_timeout_ms") != 10000 ||
            RequireUnsigned(metadata, "frame_completion_timeout_ms") != 30000 ||
            RequireUnsigned(metadata, "manual_white_point") != 1 || RequireBool(metadata, "passthrough") ||
            RequireString(metadata, "device_uuid") != device_uuid ||
            RequireString(metadata, "driver_uuid") != driver_uuid || RequireString(metadata, "status") != "READY" ||
            !RequireBool(metadata, "completed_GPU") || RequireUnsigned(metadata, "width") != width ||
            RequireUnsigned(metadata, "height") != height || RequireUnsigned(metadata, "render_width") != width ||
            RequireUnsigned(metadata, "render_height") != height)
            throw std::runtime_error("C3 metadata identity/extent/UUID mismatch");
        const auto& entries = metadata.at("resources");
        if (!entries.is_array() || entries.size() != 4)
            throw std::runtime_error("C3 metadata must contain four resources");

        std::set<std::string> roles;
        std::set<uint32_t> raw_fd_numbers;
        for (const auto& entry : entries) {
            const std::string role = entry.at("role").get<std::string>();
            if (!ExpectedFormat(role) || !roles.insert(role).second)
                throw std::runtime_error("C3 metadata has unknown or duplicate resource role");
            const auto raw_fd = JsonUnsigned(entry.at("fd"), "fd", INT32_MAX);
            if (!raw_fd || !raw_fd_numbers.insert(static_cast<uint32_t>(raw_fd)).second)
                throw std::runtime_error("C3 duplicate or invalid raw fd number");
            for (const char* key : {"format", "width", "height", "usage", "tiling",
                                    "image_type", "samples", "mip_levels", "array_layers",
                                    "flags", "sharing_mode", "offset", "runtime_layout",
                                    "memory_type_index", "req_memory_type_bits",
                                    "external_features", "compatible_handle_types"})
                (void)JsonUnsigned(entry.at(key), key);
            (void)JsonUnsigned(entry.at("allocation_size"), "allocation_size");
            (void)JsonUnsigned(entry.at("alignment"), "alignment");
            (void)RequireBool(entry, "dedicated");
            (void)JsonVkBool32(entry.at("requires_dedicated"), "requires_dedicated");
            (void)JsonVkBool32(entry.at("prefers_dedicated"), "prefers_dedicated");
        }
        if (roles != std::set<std::string>{"color", "depth", "mv", "output"})
            throw std::runtime_error("C3 metadata role set is incomplete");

        const auto device = instance.GetDevice();
        const auto physical = instance.GetPhysicalDevice();
        const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, process.ExporterPid(), 0));
        if (pidfd < 0) throw std::runtime_error("C3 pidfd_open errno=" + std::to_string(errno));
        try {
            for (const auto& entry : entries) {
                const std::string role = entry.at("role").get<std::string>();
                const uint32_t format = ExpectedFormat(role);
                if (!format || entry.at("format") != format || entry.at("width") != width ||
                    entry.at("height") != height || entry.at("usage") != 15 ||
                    entry.at("tiling") != 0 || entry.at("image_type") != 1 ||
                    entry.at("samples") != 1 || entry.at("mip_levels") != 1 ||
                    entry.at("array_layers") != 1 || entry.at("sharing_mode") != 0 ||
                    entry.at("flags") != 0 || entry.at("offset") != 0 ||
                    entry.at("dedicated") != true || entry.at("runtime_layout") != 1)
                    throw std::runtime_error("C3 per-image contract mismatch for " + role);
                const uint32_t raw_fd = entry.at("fd").get<uint32_t>();
                const auto raw_snapshot = SnapshotFds(process.ExporterPid(),
                    run_dir + "/wine-fd-before-import.json");
                const auto raw_identity = std::find_if(raw_snapshot.begin(), raw_snapshot.end(),
                    [&](const auto& record) { return record.number == static_cast<int>(raw_fd); });
                if (raw_fd > static_cast<uint32_t>(INT32_MAX) ||
                    raw_identity == raw_snapshot.end() || !raw_identity->stat_ok)
                    throw std::runtime_error("C3 raw DMA_BUF fd identity is not auditable");
                if (std::any_of(exporter_raw_fds.begin(), exporter_raw_fds.end(),
                        [&](const auto& other) { return SamePayload(other, *raw_identity); }))
                    throw std::runtime_error("C3 roles alias the same raw DMA_BUF payload");
                exporter_raw_fds.push_back(*raw_identity);
                if (!process.ExporterIdentityCurrent())
                    throw std::runtime_error("C3 exporter identity changed before pidfd_getfd");
                const int fd = static_cast<int>(syscall(SYS_pidfd_getfd, pidfd, raw_fd, 0));
                if (fd < 0)
                    throw std::runtime_error("C3 pidfd_getfd role=" + role + " errno=" +
                                             std::to_string(errno));
                const auto duplicate_snapshot = SnapshotFds(getpid(),
                    run_dir + "/native-fd-dup-check.json");
                const auto duplicate_identity = std::find_if(duplicate_snapshot.begin(),
                    duplicate_snapshot.end(), [&](const auto& record) { return record.number == fd; });
                if (duplicate_identity == duplicate_snapshot.end() ||
                    !SamePayload(*raw_identity, *duplicate_identity)) {
                    close(fd);
                    throw std::runtime_error("C3 pidfd_getfd duplicate payload differs from exporter raw fd");
                }

                const auto vk_format = static_cast<vk::Format>(format);
                const vk::ExternalMemoryImageCreateInfo external{.handleTypes = HandleType};
                const vk::ImageCreateInfo image_ci{
                    .pNext = &external,
                    .imageType = vk::ImageType::e2D,
                    .format = vk_format,
                    .extent = {width, height, 1},
                    .mipLevels = 1,
                    .arrayLayers = 1,
                    .samples = vk::SampleCountFlagBits::e1,
                    .tiling = vk::ImageTiling::eOptimal,
                    .usage = SharedUsage,
                    .sharingMode = vk::SharingMode::eExclusive,
                    .initialLayout = vk::ImageLayout::eUndefined,
                };
                const auto [create_result, image] = device.createImage(image_ci);
                if (create_result != vk::Result::eSuccess) {
                    close(fd);
                    throw std::runtime_error("C3 vkCreateImage role=" + role + " result=" +
                                             vk::to_string(create_result));
                }
                vk::MemoryDedicatedRequirements dedicated{};
                const vk::ImageMemoryRequirementsInfo2 req_info{.image = image};
                vk::MemoryRequirements2 reqs{.pNext = &dedicated};
                device.getImageMemoryRequirements2(&req_info, &reqs);
                const auto& memory_reqs = reqs.memoryRequirements;
                const auto [fd_result, fd_props] = device.getMemoryFdPropertiesKHR(HandleType, fd);
                if (fd_result != vk::Result::eSuccess) {
                    close(fd);
                    device.destroyImage(image);
                    throw std::runtime_error("C3 vkGetMemoryFdPropertiesKHR role=" + role +
                                             " result=" + vk::to_string(fd_result));
                }
                const uint32_t declared_type = entry.at("memory_type_index").get<uint32_t>();
                const uint32_t declared_bits = entry.at("req_memory_type_bits").get<uint32_t>();
                const vk::DeviceSize declared_size = entry.at("allocation_size").get<vk::DeviceSize>();
                const vk::DeviceSize declared_alignment = entry.at("alignment").get<vk::DeviceSize>();
                const uint32_t external_features = entry.at("external_features").get<uint32_t>();
                const uint32_t compatible_handles = entry.at("compatible_handle_types").get<uint32_t>();
                const uint32_t compatible = memory_reqs.memoryTypeBits & fd_props.memoryTypeBits;
                if (declared_type >= 32 || declared_bits != memory_reqs.memoryTypeBits ||
                    !(compatible & (1u << declared_type)) || declared_size != memory_reqs.size ||
                    declared_alignment != memory_reqs.alignment || declared_alignment == 0 ||
                    JsonVkBool32(entry.at("requires_dedicated"), "requires_dedicated") !=
                        static_cast<bool>(dedicated.requiresDedicatedAllocation) ||
                    JsonVkBool32(entry.at("prefers_dedicated"), "prefers_dedicated") !=
                        static_cast<bool>(dedicated.prefersDedicatedAllocation) ||
                    !(external_features & 0x2u) || !(compatible_handles & 0x200u) ||
                    (dedicated.requiresDedicatedAllocation && !entry.at("dedicated").get<bool>())) {
                    close(fd);
                    device.destroyImage(image);
                    throw std::runtime_error("C3 memory type / dedicated requirement mismatch for " + role);
                }
                const vk::MemoryDedicatedAllocateInfo dedicated_alloc{.image = image};
                const vk::ImportMemoryFdInfoKHR import_info{
                    .pNext = &dedicated_alloc, .handleType = HandleType, .fd = fd};
                const vk::MemoryAllocateInfo alloc_info{
                    .pNext = &import_info,
                    .allocationSize = declared_size,
                    .memoryTypeIndex = declared_type,
                };
                const auto [alloc_result, memory] = device.allocateMemory(alloc_info);
                if (alloc_result != vk::Result::eSuccess) {
                    close(fd);
                    device.destroyImage(image);
                    throw std::runtime_error("C3 vkAllocateMemory import role=" + role +
                                             " result=" + vk::to_string(alloc_result));
                }
                const auto bind_result = device.bindImageMemory(image, memory, 0);
                if (bind_result != vk::Result::eSuccess) {
                    device.freeMemory(memory);
                    device.destroyImage(image);
                    throw std::runtime_error("C3 vkBindImageMemory role=" + role +
                                             " result=" + vk::to_string(bind_result));
                }
                const auto [view_result, view] = device.createImageView({
                    .image = image,
                    .viewType = vk::ImageViewType::e2D,
                    .format = vk_format,
                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                });
                if (view_result != vk::Result::eSuccess) {
                    device.freeMemory(memory);
                    device.destroyImage(image);
                    throw std::runtime_error("C3 vkCreateImageView role=" + role +
                                             " result=" + vk::to_string(view_result));
                }
                resources.push_back({role, image, memory, view, vk_format,
                                     static_cast<int>(raw_fd)});
                std::printf("C3_IMPORT role=%s fd=%u bits=0x%x type=%u GENERAL_EXTERNAL=1\n",
                            role.c_str(), raw_fd, compatible, declared_type);
            }
        } catch (...) {
            close(pidfd);
            throw;
        }
        close(pidfd);
        if (!process.ExporterIdentityCurrent())
            throw std::runtime_error("C3 exporter identity changed after imports");
        auto imported_ack = Envelope(session, mode, "imported", "IMPORTED", 0, 0);
        AtomicWrite(run_dir + "/imported-" + session + "-g0.json", imported_ack.dump());
        imported = true;
        control_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        std::printf("C3_NATIVE_DEADLINE phase=fd_retired_control_ms limit=60000 start=IMPORTED_ack\n");
        if (!AuditFdRetired(instance, process)) return false;
        return true;
    } catch (const std::exception& e) {
        Quarantine(e.what());
        return false;
    }
}

vk::ImageView C3GameBridge::ProcessFrame(const Instance& instance, Scheduler& scheduler,
                                         C3NrProcess& process, vk::Image sr_color,
                                         vk::ImageView sr_view, vk::Image depth,
                                         vk::ImageView depth_view, vk::Image motion,
                                         vk::ImageView motion_view,
                                         vk::ImageLayout depth_layout,
                                         u64 source_game_frame, bool source_reset,
                                         float source_frame_ms,
                                         std::array<float, 2> viewport_jitter,
                                         std::array<float, 2> previous_viewport_jitter,
                                         std::array<float, 2> sr_jitter,
                                         std::array<float, 2> previous_sr_jitter,
                                         bool sr_initial_reset_observed,
                                         u64 sr_initial_source_game_frame,
                                         bool sr_history_jitter_zero_from_start) {
    frame_processed = false;
    if (!prepared || quarantined || finished || processed_frames >= frame_count ||
        source_game_frame == 0 || (last_source_frame && source_game_frame <= last_source_frame))
        return VK_NULL_HANDLE;
    try {
        // consumed only releases the shared slot. Its independent local-return image cannot
        // be overwritten until the previous merge's actual scheduler submission completes.
        if (pending_source_frame) {
            if (!pending_merge_tick) throw std::runtime_error("C3 prior frame lacks actual merge tick");
            if (pending_merge_tick >= scheduler.CurrentTick()) {
                last_submitted_tick = scheduler.CurrentTick();
                outstanding_tick = last_submitted_tick;
                scheduler.Flush(); // submit the recorded tick; never wait on an unsubmitted tick
            }
            if (!WaitSubmitted(scheduler, pending_merge_tick))
                throw std::runtime_error("C3 prior merge last-use fence exceeded 10s; no slot reuse");
            last_completed_tick = std::max(last_completed_tick, pending_merge_tick);
            outstanding_tick = 0;
            std::printf("C3_SLOT_REUSE frame=%u prior_source=%llu prior_merge_tick=%llu GPU_complete=1 consumed_published=1\n",
                        processed_frames, (unsigned long long)*pending_source_frame,
                        (unsigned long long)pending_merge_tick);
            pending_source_frame.reset();
            pending_merge_tick = 0;
            retirement_scheduled = false;
            returned_view = nullptr;
        }
        if (process.SetupFailed() || process.Completed() || !process.ExporterIdentityCurrent())
            throw std::runtime_error("C3 resident helper exited/changed before current input");
        if (depth_layout != vk::ImageLayout::eGeneral)
            throw std::runtime_error("C3 profile requires HDR Run depth GENERAL");
        const auto all_zero = [](const auto& value) {
            return std::isfinite(value[0]) && std::isfinite(value[1]) &&
                   value[0] == 0.0f && value[1] == 0.0f;
        };
        if (!std::isfinite(source_frame_ms) || source_frame_ms <= 0.0f ||
            !all_zero(viewport_jitter) || !all_zero(previous_viewport_jitter) ||
            !all_zero(sr_jitter) || !all_zero(previous_sr_jitter) ||
            !sr_initial_reset_observed || !sr_history_jitter_zero_from_start ||
            !sr_initial_source_game_frame || sr_initial_source_game_frame > source_game_frame)
            throw std::runtime_error("C3 real zero-jitter/fresh SR history profile gate failed");
        const u32 frame = processed_frames;
        const u64 previous_source = last_source_frame;
        const std::string reset_reason = !frame ? "first" : source_reset ? "sr_reset" :
            source_game_frame != previous_source + 1 ? "source_gap" : "continuous";
        const u32 nr_reset = reset_reason == "continuous" ? 0u : 1u;
        const bool full = frame_count == 4 || !frame || (frame + 1) % 30 == 0 || frame + 1 == frame_count;
        current_audit_full = full;
        const std::string audit_kind = full ? "full" : "gpu_full_scan";
        const bool skip = mode == "skipEvaluate";
        const u64 pixels = static_cast<u64>(width) * height, bytes32 = pixels * 16, bytes16 = pixels * 8;
        const auto& color = FindImage(resources, "color");
        const auto& scene_depth = FindImage(resources, "depth");
        const auto& mv = FindImage(resources, "mv");
        const auto& output = FindImage(resources, "output");
        const auto device = instance.GetDevice();
        auto cmd = scheduler.CommandBuffer();
        const auto family = instance.GetGraphicsQueueFamilyIndex();
        auto image_barrier = [&](vk::Image image, u32 src_family, u32 dst_family,
                                  vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                                  vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                                  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor) {
            const vk::ImageMemoryBarrier2 barrier{
                .srcStageMask = src_stage, .srcAccessMask = src_access,
                .dstStageMask = dst_stage, .dstAccessMask = dst_access,
                .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = src_family, .dstQueueFamilyIndex = dst_family,
                .image = image, .subresourceRange = {aspect, 0, 1, 0, 1}};
            cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
        };
        auto copy_readback = [&](vk::Image image, u64 offset) {
            const vk::BufferImageCopy copy{.bufferOffset = offset,
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .imageExtent = {width, height, 1}};
            cmd.copyImageToBuffer(image, vk::ImageLayout::eGeneral, readback->Handle(), copy);
        };
        auto host_barrier = [&](u64 offset, u64 size) {
            const vk::BufferMemoryBarrier2 barrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                .dstAccessMask = vk::AccessFlagBits2::eHostRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = readback->Handle(), .offset = offset, .size = size};
            cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
        };
        auto update_conversion = [&] {
            const std::array<vk::DescriptorImageInfo, 8> infos{{
                {.imageView = sr_view, .imageLayout = vk::ImageLayout::eGeneral},
                {.sampler = *depth_sampler, .imageView = depth_view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = motion_view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = color.view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = scene_depth.view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = mv.view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = *local_return_view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = output.view, .imageLayout = vk::ImageLayout::eGeneral}}};
            std::array<vk::WriteDescriptorSet, 8> writes{};
            for (u32 i = 0; i < writes.size(); ++i)
                writes[i] = {.dstSet = descriptor_set, .dstBinding = i, .descriptorCount = 1,
                    .descriptorType = i == 1 ? vk::DescriptorType::eCombinedImageSampler : vk::DescriptorType::eStorageImage,
                    .pImageInfo = &infos[i]};
            device.updateDescriptorSets(writes, {});
        };
        auto conversion = [&](u32 conversion_mode) {
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0, descriptor_set, {});
            cmd.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                              sizeof(conversion_mode), &conversion_mode);
            cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);
        };
        const auto frame_started = std::chrono::steady_clock::now();
        for (const auto& image : resources)
            image_barrier(image.image, VK_QUEUE_FAMILY_EXTERNAL, family,
                vk::PipelineStageFlagBits2::eAllCommands, {},
                vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eTransferWrite);
        for (const auto [image, aspect] : std::array<std::pair<vk::Image, vk::ImageAspectFlags>, 3>{{
                 {sr_color, vk::ImageAspectFlagBits::eColor},
                 {depth, vk::ImageAspectFlagBits::eDepth},
                 {motion, vk::ImageAspectFlagBits::eColor}}})
            image_barrier(image, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead, aspect);
        update_conversion();
        conversion(0); // real SR/Depth/MV same-texel conversion; original SR remains immutable
        const vk::ClearColorValue poison{std::array<float, 4>{-1234.f, -1234.f, -1234.f, -1234.f}};
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        cmd.clearColorImage(output.image, vk::ImageLayout::eGeneral, poison, range);
        image_barrier(color.image, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
            vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead);
        RecordValidation(instance, scheduler, color.view, color.view); // input-only: source bound twice
        if (full) { copy_readback(color.image, 0); host_barrier(0, bytes32); }
        for (const auto& image : resources)
            image_barrier(image.image, family, VK_QUEUE_FAMILY_EXTERNAL,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                vk::PipelineStageFlagBits2::eBottomOfPipe, {});
        input_tick = scheduler.CurrentTick();
        last_submitted_tick = outstanding_tick = input_tick;
        scheduler.Flush();
        if (!WaitSubmitted(scheduler, input_tick)) throw std::runtime_error("C3 input GPU deadline 10000ms");
        last_completed_tick = input_tick; outstanding_tick = 0;
        const auto input_stats = ReadValidation();
        if (input_stats.source_rgba_finite != pixels * 4 || input_stats.source_alpha_finite != pixels)
            throw std::runtime_error("C3 actual GPU source contains nonfinite values");
        std::string input_digest;
        if (full) {
            readback->Invalidate(0, bytes32);
            const auto* source = readback->mapped_data.data();
            input_digest = Hex64(Fnv1a(source, bytes32));
            const auto cpu_signature = C3SignatureRgba32(reinterpret_cast<const uint32_t*>(source), width, height);
            if (cpu_signature.xor32 != input_stats.source_xor || cpu_signature.sum32 != input_stats.source_sum)
                throw std::runtime_error("C3 independent CPU source signature disagrees with GPU");
            std::ofstream dump(run_dir + "/native-source-f" + std::to_string(frame) + ".rgba32f", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(source), bytes32);
            if (!dump) throw std::runtime_error("C3 native full source dump failed");
        }
        Json input = Envelope(session, mode, "input", "INPUT_READY", frame, source_game_frame);
        input["prev_source_game_frame"] = previous_source;
        input["history_version"] = 1u; input["params_version"] = 1u;
        input["nr_reset"] = nr_reset; input["reset_reason"] = reset_reason;
        input["audit_kind"] = audit_kind;
        input["signature_algorithm"] = "rgba32f_mix32_xor_sum_v1";
        input["input_signature_xor32"] = input_stats.source_xor;
        input["input_signature_sum32"] = input_stats.source_sum;
        input["hdr"] = true; input["exposure_source"] = "unknown"; input["exposure_valid"] = false;
        input["source_reset"] = source_reset; input["source_frame_ms"] = source_frame_ms;
        auto add_jitter = [&](std::string_view prefix, const std::array<float, 2>& value) {
            input[std::string(prefix) + "_x"] = value[0]; input[std::string(prefix) + "_y"] = value[1];
        };
        add_jitter("viewport_jitter", viewport_jitter); add_jitter("previous_viewport_jitter", previous_viewport_jitter);
        add_jitter("sr_jitter", sr_jitter); add_jitter("previous_sr_jitter", previous_sr_jitter);
        input["valid_rect"] = {{"x", 0}, {"y", 0}, {"width", width}, {"height", height}};
        input["depth_inverted"] = false; input["mv_scale_x"] = 1.0; input["mv_scale_y"] = 1.0;
        input["nr_jitter_x"] = 0.0; input["nr_jitter_y"] = 0.0;
        input["manual_white_point"] = 1.0; input["passthrough"] = false;
        input["sr_initial_reset_observed"] = sr_initial_reset_observed;
        input["sr_initial_source_game_frame"] = sr_initial_source_game_frame;
        input["sr_history_jitter_zero_from_start"] = sr_history_jitter_zero_from_start;
        if (full) {
            input["input_digest_algorithm"] = "fnv1a64_rgba32f_bytes";
            input["input_digest_bytes"] = bytes32; input["input_digest"] = input_digest;
        }
        const auto suffix = session + "-g0-f" + std::to_string(frame) + ".json";
        const auto input_path = run_dir + "/input-" + suffix;
        if (HasFile(input_path)) throw std::runtime_error("C3 duplicate input tuple");
        const auto input_wire = input.dump();
        const auto wire_input = StrictJson(input_wire, "published C3 input");
        const auto complete_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        AtomicWrite(input_path, input_wire);
        std::printf("C3_INPUT_READY mode=%s session=%s frame=%u source_game_frame=%llu prev_source_game_frame=%llu source_reset=%u nr_reset=%u reset_reason=%s audit_kind=%s signature=%u,%u input_tick=%llu actual_current_previous_jitter=0\n",
            mode.c_str(), session.c_str(), frame, (unsigned long long)source_game_frame,
            (unsigned long long)previous_source, source_reset, nr_reset, reset_reason.c_str(),
            audit_kind.c_str(), input_stats.source_xor, input_stats.source_sum, (unsigned long long)input_tick);
        const auto complete_path = run_dir + "/complete-" + suffix;
        std::string complete_text;
        while (std::chrono::steady_clock::now() < complete_deadline) {
            if (process.Completed() || process.SetupFailed()) throw std::runtime_error("C3 helper exited before completion");
            complete_text = Read(complete_path);
            if (!complete_text.empty() && std::chrono::steady_clock::now() < complete_deadline) break;
            complete_text.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (complete_text.empty() || std::chrono::steady_clock::now() >= complete_deadline) {
            AtomicWrite(run_dir + "/native-expired-" + suffix, wire_input.dump());
            throw std::runtime_error("C3 current tuple expired at 30000ms; late output forbidden");
        }
        const Json complete = StrictJson(complete_text, complete_path);
        ValidateEnvelope(complete, session, mode, "complete", skip ? "INVALID" : "OUTPUT_VALID",
                         frame, source_game_frame, !skip, !skip);
        const std::set<std::string> tuple_keys{"session", "mode", "generation", "frame", "resource_set",
            "source_game_frame", "profile_id", "width", "height", "render_width", "render_height",
            "op", "status", "completed_GPU", "output_valid", "model_valid"};
        for (const auto& [key, value] : wire_input.items()) {
            if (tuple_keys.contains(key)) continue;
            if (!complete.contains(key) || complete.at(key).type() != value.type() || complete.at(key) != value)
                throw std::runtime_error("C3 exact input echo mismatch: " + key);
        }
        const auto count = [&](const char* key) { return RequireUnsigned(complete, key); };
        const bool common_evidence = count("reset") == nr_reset &&
            count("source_rgba_finite") == pixels * 4 && count("source_alpha_finite") == pixels &&
            count("source_alpha_total") == pixels && count("visited_pixels") == pixels &&
            count("raw_rgb_total") == pixels * 3 && count("resolved_rgba_total") == pixels * 4 &&
            RequireBool(complete, "current_input_signature_match") &&
            RequireString(complete, "signature_algorithm") == "rgba32f_mix32_xor_sum_v1" &&
            count("input_signature_xor32") == input_stats.source_xor &&
            count("input_signature_sum32") == input_stats.source_sum &&
            count("output_signature_xor32") <= UINT32_MAX && count("output_signature_sum32") <= UINT32_MAX &&
            complete.at("sentinel").is_number() && complete.at("sentinel").get<double>() == -1234.0;
        const bool normal_accept = common_evidence && RequireString(complete, "status") == "OUTPUT_VALID" &&
            RequireBool(complete, "output_valid") && RequireBool(complete, "model_valid") &&
            count("evaluate_calls") == 1 && count("evaluate_result") == 1 && count("resolve_calls") == 1 &&
            count("evaluate_calls_total") == frame + 1 && count("resolve_calls_total") == frame + 1 &&
            count("raw_rgb_finite") == pixels * 3 && count("raw_rgb_written") == pixels * 3 &&
            count("raw_rgb_nan") == 0 && count("resolved_rgba_finite") == pixels * 4 &&
            count("resolved_rgba_written") == pixels * 4 && count("source_alpha_preserved") == pixels &&
            count("output_sentinel_count") == 0;
        const bool negative_evidence = common_evidence && !normal_accept &&
            RequireString(complete, "status") == "INVALID" && !RequireBool(complete, "output_valid") &&
            !RequireBool(complete, "model_valid") && count("evaluate_calls") == 0 &&
            count("evaluate_result") == 0 && count("resolve_calls") == 0 &&
            count("evaluate_calls_total") == 0 && count("resolve_calls_total") == 0 &&
            count("raw_rgb_finite") == 0 && count("raw_rgb_written") == 0 &&
            count("raw_rgb_nan") == pixels * 3 && count("resolved_rgba_finite") == pixels * 4 &&
            count("resolved_rgba_written") == 0 && count("output_sentinel_count") == pixels * 4;
        if (skip ? !negative_evidence : !normal_accept)
            throw std::runtime_error("C3 per-frame actual model/poison/cumulative acceptance gate failed");
        if (full) {
            if (!RequireBool(complete, "current_input_digest_match") ||
                RequireString(complete, "input_digest") != input_digest ||
                RequireString(complete, "input_digest_algorithm") != "fnv1a64_rgba32f_bytes" ||
                count("input_digest_bytes") != bytes32 || count("output_digest_bytes") != bytes32 ||
                RequireString(complete, "output_digest_algorithm") != "fnv1a64_rgba32f_bytes")
                throw std::runtime_error("C3 scheduled full FNV evidence mismatch");
        } else {
            for (const auto& [key, value] : complete.items())
                if (key.find("digest") != std::string::npos)
                    throw std::runtime_error("C3 non-full response contains prohibited digest field");
        }
        if (std::chrono::steady_clock::now() >= complete_deadline)
            throw std::runtime_error("C3 completion verification exceeded active deadline; not accepted");
        cmd = scheduler.CommandBuffer(); // previous input Flush invalidated every old command handle
        for (const auto* image : {&color, &output})
            image_barrier(image->image, VK_QUEUE_FAMILY_EXTERNAL, family,
                vk::PipelineStageFlagBits2::eAllCommands, {},
                vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead);
        if (full) { copy_readback(output.image, bytes32); host_barrier(bytes32, bytes32); }
        vk::ImageView accepted_view{};
        if (!skip) {
            const vk::ImageMemoryBarrier2 to_write{
                .srcStageMask = local_return_initialized ? vk::PipelineStageFlagBits2::eAllCommands : vk::PipelineStageFlagBits2::eTopOfPipe,
                .srcAccessMask = local_return_initialized ? vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite : vk::AccessFlags2{},
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader, .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
                .oldLayout = local_return_initialized ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk::Image(local_return), .subresourceRange = range};
            cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_write});
            update_conversion(); conversion(1);
            if (full) {
                image_barrier(vk::Image(local_return), VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
                copy_readback(vk::Image(local_return), bytes32 * 2); host_barrier(bytes32 * 2, bytes16);
            }
            accepted_view = *local_return_view;
        }
        if (!skip) {
            image_barrier(vk::Image(local_return), VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
        }
        RecordValidation(instance, scheduler, color.view, output.view, !skip);
        for (const auto* image : {&color, &output})
            image_barrier(image->image, family, VK_QUEUE_FAMILY_EXTERNAL,
                vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eBottomOfPipe, {});
        const u64 output_tick = scheduler.CurrentTick();
        last_submitted_tick = outstanding_tick = output_tick;
        scheduler.Flush();
        if (!WaitSubmitted(scheduler, output_tick)) throw std::runtime_error("C3 output/validation GPU deadline10000ms");
        last_completed_tick = output_tick; outstanding_tick = 0;
        const auto output_stats = ReadValidation();
        std::array<u32, 4> half_gpu_stats{};
        if (!skip) {
            return_validation_stats->Invalidate(0, 16);
            std::memcpy(half_gpu_stats.data(), return_validation_stats->mapped_data.data(), 16);
            if (half_gpu_stats[0] != pixels * 4 || half_gpu_stats[1] != pixels ||
                half_gpu_stats[2] || half_gpu_stats[3])
                throw std::runtime_error("C3 actual returned RGBA16F full-GPU finite guard failed; SR fallback");
        }
        if (output_stats.source_rgba_finite != pixels * 4 || output_stats.source_alpha_finite != pixels ||
            output_stats.source_xor != input_stats.source_xor || output_stats.source_sum != input_stats.source_sum ||
            output_stats.out_xor != count("output_signature_xor32") || output_stats.out_sum != count("output_signature_sum32") ||
            output_stats.out_rgba_finite != pixels * 4 ||
            (skip ? (output_stats.out_sentinel_count != pixels * 4 || output_stats.out_rgba_written != 0) :
                    (output_stats.out_sentinel_count != 0 || output_stats.out_rgba_written != pixels * 4 || output_stats.alpha_bits_equal != pixels)))
            throw std::runtime_error("C3 actual native full-GPU source/output/signature/alpha validation failed");
        if (full) {
            readback->Invalidate(0, bytes32 * 2 + (skip ? 0 : bytes16));
            const auto* source = readback->mapped_data.data();
            const auto* out = source + bytes32;
            const auto output_digest = Hex64(Fnv1a(out, bytes32));
            if (output_digest != RequireString(complete, "output_digest"))
                throw std::runtime_error("C3 native output full FNV differs from Wine");
            const auto cpu_signature = C3SignatureRgba32(reinterpret_cast<const uint32_t*>(out), width, height);
            if (cpu_signature.xor32 != output_stats.out_xor || cpu_signature.sum32 != output_stats.out_sum)
                throw std::runtime_error("C3 CPU output signature differs from actual GPU");
            const auto* source_words = reinterpret_cast<const uint32_t*>(source);
            const auto* out_words = reinterpret_cast<const uint32_t*>(out);
            u64 finite = 0, written = 0, alpha = 0;
            for (u64 i = 0; i < pixels * 4; ++i) {
                float value; std::memcpy(&value, &out_words[i], 4);
                finite += std::isfinite(value); written += std::isfinite(value) && value != -1234.f;
                if ((i & 3) == 3) alpha += source_words[i] == out_words[i];
            }
            if (finite != output_stats.out_rgba_finite || written != output_stats.out_rgba_written ||
                alpha != output_stats.alpha_bits_equal)
                throw std::runtime_error("C3 scheduled full CPU output counters differ from GPU");
            std::ofstream dump(run_dir + "/native-resolve-f" + std::to_string(frame) + ".rgba32f", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(out), bytes32);
            if (!dump) throw std::runtime_error("C3 native output dump failed");
            if (!skip) {
                const auto* half = reinterpret_cast<const uint16_t*>(source + bytes32 * 2);
                u64 half_finite = 0;
                for (u64 i = 0; i < pixels * 4; ++i) half_finite += (half[i] & 0x7c00u) != 0x7c00u;
                if (half_finite != pixels * 4 || half_finite != half_gpu_stats[0]) throw std::runtime_error("C3 scheduled return-half CPU/GPU finite audit failed");
                std::ofstream half_dump(run_dir + "/native-return-f" + std::to_string(frame) + ".rgba16f", std::ios::binary);
                half_dump.write(reinterpret_cast<const char*>(half), bytes16);
                if (!half_dump) throw std::runtime_error("C3 native return-half dump failed");
            }
        }
        if (std::chrono::steady_clock::now() >= complete_deadline)
            throw std::runtime_error("C3 current-frame validation exceeded deadline; no acceptance");
        if (!process.ExporterIdentityCurrent()) throw std::runtime_error("C3 helper changed before consumed");
        Json consumed = Envelope(session, mode, "consumed", "CONSUMED", frame, source_game_frame);
        for (const char* key : {"prev_source_game_frame", "history_version", "params_version", "nr_reset", "reset_reason", "audit_kind"})
            consumed[key] = wire_input.at(key);
        const auto consumed_path = run_dir + "/consumed-" + suffix;
        if (HasFile(consumed_path)) throw std::runtime_error("C3 duplicate consumed tuple");
        AtomicWrite(consumed_path, consumed.dump());
        local_return_initialized |= !skip;
        pending_source_frame = source_game_frame; last_source_frame = source_game_frame;
        ++processed_frames; frame_processed = true; returned_view = accepted_view;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frame_started).count();
        Json frame_record = consumed;
        frame_record["status"] = skip ? "INVALID_REJECTED" : "ACCEPTED";
        frame_record["op"] = "native_validation"; frame_record["selected_output"] = !skip;
        frame_record["signature_algorithm"] = "rgba32f_mix32_xor_sum_v1";
        frame_record["input_signature_xor32"] = input_stats.source_xor; frame_record["input_signature_sum32"] = input_stats.source_sum;
        frame_record["output_signature_xor32"] = output_stats.out_xor; frame_record["output_signature_sum32"] = output_stats.out_sum;
        frame_record["native_GPU_stats"] = {{"source_rgba_finite", output_stats.source_rgba_finite},
            {"source_alpha_finite", output_stats.source_alpha_finite}, {"out_rgba_finite", output_stats.out_rgba_finite},
            {"out_rgba_written", output_stats.out_rgba_written}, {"alpha_bits_equal", output_stats.alpha_bits_equal},
            {"output_sentinel_count", output_stats.out_sentinel_count}, {"visited_pixels", output_stats.visited_pixels}};
        frame_record["native_return_half_finite"] = half_gpu_stats[0];
        frame_record["native_return_half_visited"] = half_gpu_stats[1];
        frame_record["input_tick"] = input_tick; frame_record["output_tick"] = output_tick;
        frame_record["native_elapsed_ms"] = elapsed; frame_record["audit_readback_bytes"] = full ? bytes32 * 2 + (skip ? 0 : bytes16) + (skip ? 128 : 144) : (skip ? 128 : 144);
        AtomicWrite(run_dir + "/native-frame-" + suffix, frame_record.dump());
        std::printf("C3_FRAME_%s mode=%s session=%s frame=%u source_game_frame=%llu prev_source_game_frame=%llu nr_reset=%u reset_reason=%s audit_kind=%s input_signature=%u,%u output_signature=%u,%u native_GPU_finite=%u alpha_equal=%u sentinel=%u tick_source=%llu tick_output=%llu elapsed_ms=%lld SR_fallback=%u\n",
            skip ? "INVALID_REJECTED" : "ACCEPTED", mode.c_str(), session.c_str(), frame,
            (unsigned long long)source_game_frame, (unsigned long long)previous_source, nr_reset,
            reset_reason.c_str(), audit_kind.c_str(), input_stats.source_xor, input_stats.source_sum,
            output_stats.out_xor, output_stats.out_sum, output_stats.out_rgba_finite,
            output_stats.alpha_bits_equal, output_stats.out_sentinel_count,
            (unsigned long long)input_tick, (unsigned long long)output_tick, (long long)elapsed, skip);
        return returned_view;
    } catch (const std::exception& e) {
        Quarantine(e.what());
        return VK_NULL_HANDLE;
    }
}

void C3GameBridge::PrepareReturnForMerge(Scheduler& scheduler) {
    if (!frame_processed || !returned_view || quarantined || mode != "resident")
        throw std::runtime_error("C3 return-image dependency requested without accepted resident output");
    const vk::ImageMemoryBarrier2 return_to_merge{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader |
                        vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite |
                         vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk::Image(local_return),
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    scheduler.CommandBuffer().pipelineBarrier2({.imageMemoryBarrierCount = 1,
                                                 .pImageMemoryBarriers = &return_to_merge});
    std::printf("C3_RETURN_MERGE_BARRIER mode=resident source_game_frame=%llu src=COMPUTE|TRANSFER/SHADER_WRITE|TRANSFER_READ dst=COMPUTE/SHADER_READ\n",
                static_cast<unsigned long long>(pending_source_frame.value_or(0)));
}

void C3GameBridge::DeferRetirement(const Instance& instance, Scheduler& scheduler) {
    if (!pending_source_frame || retirement_scheduled || quarantined || finished) return;
    pending_merge_tick = scheduler.CurrentTick();
    std::printf("C3_MERGE_LAST_USE frame=%u source_game_frame=%llu tick=%llu submitted_yet=0 final=%u\n",
                processed_frames - 1, (unsigned long long)*pending_source_frame,
                (unsigned long long)pending_merge_tick, processed_frames == frame_count);
    if (processed_frames != frame_count) return;
    retirement_scheduled = true;
    const u32 terminal_frame = frame_count;
    const auto device = instance.GetDevice();
    const auto source_frame = *pending_source_frame;
    const std::string run_dir_copy = run_dir;
    const std::string session_copy = session;
    const std::string mode_copy = mode;
    const auto raw_fds = exporter_raw_fds;
    const auto exporter_fds_at_baseline = exporter_baseline_fds;
    const pid_t verified_exporter_pid = exporter_pid;
    scheduler.DeferOperation([this, device, source_frame, run_dir_copy, session_copy, mode_copy,
                              raw_fds, exporter_fds_at_baseline, verified_exporter_pid, terminal_frame] {
        try {
            // This callback is tied to the merge submission tick: the local return image and
            // its view remain alive until the merge no longer references them.
            local_return_view.reset();
            local_return = VideoCore::UniqueImage{};
            returned_view = nullptr;
            for (auto& resource : resources) {
                if (resource.view) device.destroyImageView(resource.view);
                if (resource.image) device.destroyImage(resource.image);
                if (resource.memory) device.freeMemory(resource.memory);
            }
            resources.clear();
            auto release = Envelope(session_copy, mode_copy, "release", "NATIVE_RELEASED", terminal_frame, source_frame);
            AtomicWrite(run_dir_copy + "/native-released-" + session_copy + "-g0.json",
                        release.dump());
            pending_source_frame.reset();
            finished = true;
            std::thread([run_dir_copy, session_copy, mode_copy, source_frame, raw_fds,
                         exporter_fds_at_baseline,
                         verified_exporter_pid, terminal_frame] {
                try {
                    const auto retired_text = WaitJson(
                        run_dir_copy + "/retired-" + session_copy + "-g0.json",
                        std::chrono::seconds(60));
                    if (retired_text.empty())
                        throw std::runtime_error("C3 helper retirement control deadline 60000ms");
                    const Json retired = StrictJson(retired_text, run_dir_copy + "/retired");
                    ValidateEnvelope(retired, session_copy, mode_copy, "retired", "RETIRED", terminal_frame,
                                     source_frame);
                    const auto final_fds = SnapshotFds(getpid(),
                        run_dir_copy + "/native-fd-final.json");
                    const auto wine_fds = SnapshotFds(verified_exporter_pid,
                        run_dir_copy + "/wine-fd-final.json");
                    for (const auto& original : raw_fds) {
                        if (std::any_of(final_fds.begin(), final_fds.end(),
                                [&](const auto& record) { return SamePayload(original, record); }) ||
                            std::any_of(wine_fds.begin(), wine_fds.end(),
                                [&](const auto& record) { return SamePayload(original, record); }))
                            throw std::runtime_error("C3 raw export payload remains after retirement");
                    }
                    Json added = Json::array();
                    Json removed = Json::array();
                    for (const auto& record : wine_fds) {
                        if (std::none_of(exporter_fds_at_baseline.begin(),
                                exporter_fds_at_baseline.end(), [&](const auto& before) {
                                    return SameInventoryEntry(record, before);
                                })) added.push_back(FdInventoryJson({record}).front());
                    }
                    for (const auto& record : exporter_fds_at_baseline) {
                        if (std::none_of(wine_fds.begin(), wine_fds.end(),
                                [&](const auto& after) {
                                    return SameInventoryEntry(record, after);
                                })) removed.push_back(FdInventoryJson({record}).front());
                    }
                    AtomicWrite(run_dir_copy + "/wine-fd-baseline-final-diff.json",
                        Json{{"baseline_count", exporter_fds_at_baseline.size()},
                             {"final_count", wine_fds.size()},
                             {"added", added}, {"removed", removed}}.dump());
                    Json audit = Envelope(session_copy, mode_copy, "audit", "FD_AUDITED", terminal_frame, source_frame);
                    audit["fd_count"] = final_fds.size();
                    audit["wine_fd_baseline_count"] = exporter_fds_at_baseline.size();
                    audit["wine_fd_final_count"] = wine_fds.size();
                    audit["wine_fd_inventory_equal"] =
                        exporter_fds_at_baseline.size() == wine_fds.size() &&
                        added.empty() && removed.empty();
                    audit["inventory"] = "native-fd-final.json";
                    audit["wine_inventory"] = "wine-fd-final.json";
                    AtomicWrite(run_dir_copy + "/native-audited-" + session_copy + "-g0.json",
                                audit.dump());
                    std::printf("C3_RELEASE PASS source_game_frame=%llu merge_tick_completed=1 "
                                "raw_export_payloads_remaining=0\n",
                                static_cast<unsigned long long>(source_frame));
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "C3_RELEASE_AUDIT BLOCKED reason=%s\n", e.what());
                }
            }).detach();
        } catch (const std::exception& e) {
            Quarantine(e.what());
        }
    });
}

} // namespace Vulkan
