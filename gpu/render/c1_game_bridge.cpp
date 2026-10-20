// SPDX-License-Identifier: GPL-2.0-or-later
#include "c1_game_bridge.h"

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
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include "render/c1_noop_process.h"
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include "vulkan/shader_module.h"
#include "bbgpu/shaders/c1_game_bridge_comp.h"

namespace Vulkan {
namespace {
using Json = nlohmann::json;
constexpr vk::ExternalMemoryHandleTypeFlagBits HandleType =
    vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT;
constexpr vk::ImageUsageFlags SharedUsage =
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

std::string Read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), {}) : std::string{};
}

void AtomicWrite(const std::string& path, const std::string& value) {
    const auto temporary = path + ".tmp-" + std::to_string(getpid());
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("C1 open temp control: " + temporary);
    output << value << '\n';
    output.flush();
    if (!output) throw std::runtime_error("C1 write temp control: " + temporary);
    output.close();
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        throw std::runtime_error("C1 publish control: " + std::string(std::strerror(errno)));
    }
}

std::vector<C1FdIdentity> SnapshotFds(pid_t pid, const std::string& output_path) {
    const auto root = std::filesystem::path("/proc") / std::to_string(pid) / "fd";
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("C1 fd inventory unavailable for pid=" + std::to_string(pid));
    std::vector<C1FdIdentity> records;
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
        C1FdIdentity record;
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

bool SamePayload(const C1FdIdentity& a, const C1FdIdentity& b) {
    return a.stat_ok && b.stat_ok && a.target == b.target && a.device == b.device &&
           a.inode == b.inode;
}

bool SameInventoryEntry(const C1FdIdentity& a, const C1FdIdentity& b) {
    return a.number == b.number && a.target == b.target && a.stat_ok == b.stat_ok &&
           a.device == b.device && a.inode == b.inode;
}

Json FdInventoryJson(const std::vector<C1FdIdentity>& records) {
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

std::optional<Json> TryReadJson(const std::string& path) {
    if (!HasFile(path)) return std::nullopt;
    return Json::parse(Read(path));
}

std::string WaitJson(const std::string& path, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto contents = Read(path);
        if (!contents.empty()) return contents;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return {};
}

int DuplicateRemoteFd(pid_t exporter, int remote_fd) {
    if (exporter <= 1 || remote_fd < 0) throw std::runtime_error("C1 invalid exporter fd");
    const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, exporter, 0));
    if (pidfd < 0) throw std::runtime_error("C1 pidfd_open errno=" + std::to_string(errno));
    const int duplicate = static_cast<int>(syscall(SYS_pidfd_getfd, pidfd, remote_fd, 0));
    const int error = errno;
    close(pidfd);
    if (duplicate < 0) throw std::runtime_error("C1 pidfd_getfd errno=" + std::to_string(error));
    return duplicate;
}

uint32_t ExpectedFormat(std::string_view role) {
    if (role == "color" || role == "output") return 109; // R32G32B32A32_SFLOAT
    if (role == "depth") return 100;                     // R32_SFLOAT
    if (role == "mv") return 103;                        // R32G32_SFLOAT
    return 0;
}

Json Envelope(const std::string& session, std::string_view op, std::string_view status,
              u32 frame, u64 source_frame) {
    return Json{{"session", session}, {"generation", 0}, {"frame", frame},
                {"resource_set", session + "-g0"}, {"source_game_frame", source_frame},
                {"profile_id", "hdr-run-dlaa-1to1-v1"}, {"width", 1920}, {"height", 1080},
                {"render_width", 1920}, {"render_height", 1080}, {"op", op},
                {"status", status}, {"completed_GPU", true}, {"output_valid", false},
                {"model_valid", false}};
}

void ValidateEnvelope(const Json& value, const std::string& session, std::string_view op,
                      std::string_view status, u32 frame, u64 source_frame) {
    if (value.at("session") != session || value.at("generation") != 0 ||
        value.at("frame") != frame || value.at("resource_set") != session + "-g0" ||
        value.at("source_game_frame") != source_frame ||
        value.at("profile_id") != "hdr-run-dlaa-1to1-v1" || value.at("width") != 1920 ||
        value.at("height") != 1080 || value.at("render_width") != 1920 ||
        value.at("render_height") != 1080 || value.at("op") != op ||
        value.at("status") != status || value.at("completed_GPU") != true ||
        value.at("output_valid") != false || value.at("model_valid") != false)
        throw std::runtime_error("C1 control tuple/status mismatch");
}

vk::ImageView FindView(const std::vector<C1ImportedImage>& images, std::string_view role) {
    for (const auto& image : images) if (image.role == role) return image.view;
    return {};
}

C1ImportedImage& FindImage(std::vector<C1ImportedImage>& images, std::string_view role) {
    for (auto& image : images) if (image.role == role) return image;
    throw std::runtime_error("C1 image role absent: " + std::string(role));
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
        throw std::runtime_error("C1 metadata VkBool32 has invalid JSON type: " +
                                 std::string(field));
    const auto integer = value.get<int64_t>();
    if (integer != 0 && integer != 1)
        throw std::runtime_error("C1 metadata VkBool32 is outside 0/1: " +
                                 std::string(field));
    return integer == 1;
}

uint64_t JsonUnsigned(const Json& value, std::string_view field, uint64_t maximum = UINT64_MAX) {
    if (!value.is_number_unsigned() && !value.is_number_integer())
        throw std::runtime_error("C1 metadata integer has invalid JSON type: " +
                                 std::string(field));
    if (value.is_number_integer() && value.get<int64_t>() < 0)
        throw std::runtime_error("C1 metadata integer is negative: " + std::string(field));
    const uint64_t result = value.get<uint64_t>();
    if (result > maximum)
        throw std::runtime_error("C1 metadata integer out of range: " + std::string(field));
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

void C1GameBridge::Quarantine(std::string_view reason) {
    quarantined = true;
    const std::string message = "C1_QUARANTINE reason=" + std::string(reason) +
        " resources_retained=" + std::to_string(resources.size()) +
        " last_submitted_tick=" + std::to_string(last_submitted_tick) +
        " last_completed_tick=" + std::to_string(last_completed_tick) +
        " outstanding_tick=" + std::to_string(outstanding_tick) + "\n";
    std::fprintf(stderr, "%s", message.c_str());
    if (process_owner) {
        std::fprintf(stderr,
            "C1_PE_STATE child_pid=%d exporter_pid=%d completed=%u exit_status=%d identity_current=%u\n",
            process_owner->ChildPid(), process_owner->ExporterPid(), process_owner->Completed(),
            process_owner->ExitStatus(), process_owner->ExporterIdentityCurrent());
    }
    if (!run_dir.empty() && !abort_sent) {
        try {
            AtomicWrite(run_dir + "/native.abort", message);
            abort_sent = true;
            std::fprintf(stderr, "C1_HELPER_ABORT published=1 exporter_pid=%d\n", exporter_pid);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "C1_HELPER_ABORT published=0 reason=%s\n", e.what());
        }
    }
}

void C1GameBridge::CreateLocalReturnAndPipeline(const Instance& instance) {
    local_return = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
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
    const auto module = CompileSPV(C1_GAME_BRIDGE_COMP, instance.GetDevice());
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
        instance, 0, pixels * 16 * 2, VideoCore::MemoryType::HostCached,
        "C1 no-op audit readback");
    if (readback->mapped_data.size() < pixels * 32)
        throw std::runtime_error("C1 host readback buffer is not mapped to requested size");
}

bool C1GameBridge::AuditFdRetired(const Instance& instance, const C1NoopProcess& process) {
    try {
        const auto path = run_dir + "/fd-retired-" + session + "-g0.json";
        if (!HasFile(path)) return false;
        const Json fd_retired = Json::parse(Read(path));
        ValidateEnvelope(fd_retired, session, "fd_retired", "FD_RETIRED", 0, 0);
        if (!fd_retired.at("closed_fd").is_array() || fd_retired.at("closed_fd").size() != 4 ||
            resources.size() != 4 || exporter_raw_fds.size() != 4)
            throw std::runtime_error("C1 fd-retired must map four imported role/fd pairs");
        std::set<std::string> closed_roles;
        for (const auto& item : fd_retired.at("closed_fd")) {
            const auto role = item.at("role").get<std::string>();
            const auto fd = item.at("fd").get<int>();
            const auto resource = std::find_if(resources.begin(), resources.end(),
                [&](const auto& image) { return image.role == role && image.raw_fd == fd; });
            if (!closed_roles.insert(role).second || resource == resources.end())
                throw std::runtime_error("C1 fd-retired role does not match imported raw fd");
            const auto original = std::find_if(exporter_raw_fds.begin(), exporter_raw_fds.end(),
                [&](const auto& record) { return record.number == fd; });
            if (original == exporter_raw_fds.end() || !original->stat_ok)
                throw std::runtime_error("C1 original raw FD identity missing");
        }
        if (closed_roles != std::set<std::string>{"color", "depth", "mv", "output"})
            throw std::runtime_error("C1 fd-retired role set incomplete");
        const auto after = SnapshotFds(process.ExporterPid(),
            run_dir + "/wine-fd-active-retired.json");
        for (const auto& original : exporter_raw_fds) {
            if (std::any_of(after.begin(), after.end(), [&](const auto& record) {
                    return SamePayload(original, record);
                }))
                throw std::runtime_error("C1 original raw DMA_BUF payload remains after consume");
        }
        fd_retired_ready = true;
        CreateLocalReturnAndPipeline(instance);
        prepared = true;
        std::printf("C1_IMPORT_GATE PASS session=%s resources=4 fd_retired_role_payloads=4\n",
                    session.c_str());
        return true;
    } catch (const std::exception& e) {
        Quarantine(e.what());
        return false;
    }
}

bool C1GameBridge::Prepare(const Instance& instance, Scheduler& scheduler,
                           C1NoopProcess& process, std::string_view requested_session,
                           const std::string& requested_run_dir,
                           const std::string& device_uuid, const std::string& driver_uuid,
                           u32 requested_width, u32 requested_height) {
    if (prepared || quarantined || finished) return prepared && !quarantined;
    if (requested_width != 1920 || requested_height != 1080 ||
        !process.SetupReady() || !process.ExporterIdentityCurrent()) return false;
    process_owner = &process;
    if (!imported && (session.empty() || run_dir.empty())) {
        session = requested_session;
        run_dir = requested_run_dir;
        width = requested_width;
        height = requested_height;
    }
    if (session != requested_session || run_dir != requested_run_dir || width != requested_width ||
        height != requested_height) {
        Quarantine("C1 session/profile changed while slot is active");
        return false;
    }
    exporter_pid = process.ExporterPid();
    if (imported) {
        if (!fd_retired_ready) {
            if (!HasFile(run_dir + "/fd-retired-" + session + "-g0.json")) return false;
            if (!AuditFdRetired(instance, process)) return false;
            return true;
        }
        return prepared;
    }
    const auto baseline_path = run_dir + "/baseline-" + session + "-g0.json";
    if (!HasFile(baseline_path)) return false;
    try {
        if (!baseline_audited) {
            const Json baseline = Json::parse(Read(baseline_path));
            ValidateEnvelope(baseline, session, "baseline", "BASELINE", 0, 0);
            const auto baseline_fds = SnapshotFds(getpid(),
                run_dir + "/native-fd-baseline.json");
            exporter_baseline_fds = SnapshotFds(process.ExporterPid(),
                run_dir + "/wine-fd-baseline.json");
            auto baseline_ack = Envelope(session, "baseline_audit", "BASELINE_AUDITED", 0, 0);
            baseline_ack["baseline_fd_count"] = baseline_fds.size();
            baseline_ack["inventory"] = "native-fd-baseline.json";
            AtomicWrite(run_dir + "/native-baseline-" + session + "-g0.json",
                        baseline_ack.dump());
            baseline_audited = true;
            return false;
        }

        const auto metadata_path = run_dir + "/meta-g0.json";
        if (!HasFile(metadata_path)) return false;
        const Json metadata = Json::parse(Read(metadata_path));
        if (metadata.at("session") != session || metadata.at("generation") != 0 ||
            metadata.at("resource_set") != session + "-g0" ||
            metadata.at("linux_pid") != process.ExporterPid() ||
            metadata.at("handle_type") != static_cast<uint32_t>(HandleType) ||
            metadata.at("op") != "metadata" || metadata.at("profile_id") !=
                "hdr-run-dlaa-1to1-v1" || metadata.at("frame") != 0 ||
            metadata.at("source_game_frame") != 0 || metadata.at("output_valid") != false ||
            metadata.at("model_valid") != false || metadata.at("max_frames") != 1 ||
            metadata.at("control_timeout_ms") != 60000 || metadata.at("gpu_timeout_ms") != 10000 ||
            metadata.at("device_uuid") != device_uuid ||
            metadata.at("driver_uuid") != driver_uuid || metadata.at("status") != "READY" ||
            metadata.at("completed_GPU") != true || metadata.at("width") != width ||
            metadata.at("height") != height || metadata.at("render_width") != width ||
            metadata.at("render_height") != height)
            throw std::runtime_error("C1 metadata identity/extent/UUID mismatch");
        const auto& entries = metadata.at("resources");
        if (!entries.is_array() || entries.size() != 4)
            throw std::runtime_error("C1 metadata must contain four resources");

        std::set<std::string> roles;
        std::set<uint32_t> raw_fd_numbers;
        for (const auto& entry : entries) {
            const std::string role = entry.at("role").get<std::string>();
            if (!ExpectedFormat(role) || !roles.insert(role).second)
                throw std::runtime_error("C1 metadata has unknown or duplicate resource role");
            const auto raw_fd = JsonUnsigned(entry.at("fd"), "fd", INT32_MAX);
            if (!raw_fd || !raw_fd_numbers.insert(static_cast<uint32_t>(raw_fd)).second)
                throw std::runtime_error("C1 duplicate or invalid raw fd number");
            for (const char* key : {"format", "width", "height", "usage", "tiling",
                                    "image_type", "samples", "mip_levels", "array_layers",
                                    "flags", "sharing_mode", "offset", "runtime_layout",
                                    "memory_type_index", "req_memory_type_bits",
                                    "external_features", "compatible_handle_types"})
                (void)JsonUnsigned(entry.at(key), key);
            (void)JsonUnsigned(entry.at("allocation_size"), "allocation_size");
            (void)JsonUnsigned(entry.at("alignment"), "alignment");
            if (!entry.at("dedicated").is_boolean())
                throw std::runtime_error("C1 dedicated metadata must be JSON boolean");
            (void)JsonVkBool32(entry.at("requires_dedicated"), "requires_dedicated");
            (void)JsonVkBool32(entry.at("prefers_dedicated"), "prefers_dedicated");
        }
        if (roles != std::set<std::string>{"color", "depth", "mv", "output"})
            throw std::runtime_error("C1 metadata role set is incomplete");

        const auto device = instance.GetDevice();
        const auto physical = instance.GetPhysicalDevice();
        const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, process.ExporterPid(), 0));
        if (pidfd < 0) throw std::runtime_error("C1 pidfd_open errno=" + std::to_string(errno));
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
                    throw std::runtime_error("C1 per-image contract mismatch for " + role);
                const uint32_t raw_fd = entry.at("fd").get<uint32_t>();
                const auto raw_snapshot = SnapshotFds(process.ExporterPid(),
                    run_dir + "/wine-fd-before-import.json");
                const auto raw_identity = std::find_if(raw_snapshot.begin(), raw_snapshot.end(),
                    [&](const auto& record) { return record.number == static_cast<int>(raw_fd); });
                if (raw_fd > static_cast<uint32_t>(INT32_MAX) ||
                    raw_identity == raw_snapshot.end() || !raw_identity->stat_ok)
                    throw std::runtime_error("C1 raw DMA_BUF fd identity is not auditable");
                if (std::any_of(exporter_raw_fds.begin(), exporter_raw_fds.end(),
                        [&](const auto& other) { return SamePayload(other, *raw_identity); }))
                    throw std::runtime_error("C1 roles alias the same raw DMA_BUF payload");
                exporter_raw_fds.push_back(*raw_identity);
                if (!process.ExporterIdentityCurrent())
                    throw std::runtime_error("C1 exporter identity changed before pidfd_getfd");
                const int fd = static_cast<int>(syscall(SYS_pidfd_getfd, pidfd, raw_fd, 0));
                if (fd < 0)
                    throw std::runtime_error("C1 pidfd_getfd role=" + role + " errno=" +
                                             std::to_string(errno));
                const auto duplicate_snapshot = SnapshotFds(getpid(),
                    run_dir + "/native-fd-dup-check.json");
                const auto duplicate_identity = std::find_if(duplicate_snapshot.begin(),
                    duplicate_snapshot.end(), [&](const auto& record) { return record.number == fd; });
                if (duplicate_identity == duplicate_snapshot.end() ||
                    !SamePayload(*raw_identity, *duplicate_identity)) {
                    close(fd);
                    throw std::runtime_error("C1 pidfd_getfd duplicate payload differs from exporter raw fd");
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
                    throw std::runtime_error("C1 vkCreateImage role=" + role + " result=" +
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
                    throw std::runtime_error("C1 vkGetMemoryFdPropertiesKHR role=" + role +
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
                    throw std::runtime_error("C1 memory type / dedicated requirement mismatch for " + role);
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
                    throw std::runtime_error("C1 vkAllocateMemory import role=" + role +
                                             " result=" + vk::to_string(alloc_result));
                }
                const auto bind_result = device.bindImageMemory(image, memory, 0);
                if (bind_result != vk::Result::eSuccess) {
                    device.freeMemory(memory);
                    device.destroyImage(image);
                    throw std::runtime_error("C1 vkBindImageMemory role=" + role +
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
                    throw std::runtime_error("C1 vkCreateImageView role=" + role +
                                             " result=" + vk::to_string(view_result));
                }
                resources.push_back({role, image, memory, view, vk_format,
                                     static_cast<int>(raw_fd)});
                std::printf("C1_IMPORT role=%s fd=%u bits=0x%x type=%u GENERAL_EXTERNAL=1\n",
                            role.c_str(), raw_fd, compatible, declared_type);
            }
        } catch (...) {
            close(pidfd);
            throw;
        }
        close(pidfd);
        if (!process.ExporterIdentityCurrent())
            throw std::runtime_error("C1 exporter identity changed after imports");
        auto imported_ack = Envelope(session, "imported", "IMPORTED", 0, 0);
        AtomicWrite(run_dir + "/imported-" + session + "-g0.json", imported_ack.dump());
        imported = true;
        if (!AuditFdRetired(instance, process)) return false;
        return true;
    } catch (const std::exception& e) {
        Quarantine(e.what());
        return false;
    }
}

vk::ImageView C1GameBridge::ProcessFrame(const Instance& instance, Scheduler& scheduler,
                                         C1NoopProcess& process, vk::Image sr_color,
                                         vk::ImageView sr_view, vk::Image depth,
                                         vk::ImageView depth_view, vk::Image motion,
                                         vk::ImageView motion_view,
                                         vk::ImageLayout depth_layout,
                                         u64 source_game_frame, bool source_reset,
                                         std::array<float, 2> viewport_jitter,
                                         std::array<float, 2> sr_jitter) {
    if (!prepared || quarantined || finished || pending_source_frame ||
        (last_source_frame && source_game_frame <= last_source_frame)) return VK_NULL_HANDLE;
    try {
        if (depth_layout != vk::ImageLayout::eGeneral)
            throw std::runtime_error("C1 capture requires the verified HDR Run GENERAL depth layout");
        const u64 bytes32 = static_cast<u64>(width) * height * 16;
        const u64 bytes16 = static_cast<u64>(width) * height * 8;
        const auto& color = FindImage(resources, "color");
        const auto& scene_depth = FindImage(resources, "depth");
        const auto& mv = FindImage(resources, "mv");
        const auto& output = FindImage(resources, "output");
        const auto device = instance.GetDevice();
        auto cmd = scheduler.CommandBuffer();
        const auto queue_family = instance.GetGraphicsQueueFamilyIndex();

        auto transfer = [&](vk::Image image, u32 src_family, u32 dst_family,
                            vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                            vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                            vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor) {
            const vk::ImageMemoryBarrier2 barrier{
                .srcStageMask = src_stage, .srcAccessMask = src_access,
                .dstStageMask = dst_stage, .dstAccessMask = dst_access,
                .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = src_family, .dstQueueFamilyIndex = dst_family,
                .image = image,
                .subresourceRange = {aspect, 0, 1, 0, 1},
            };
            const vk::DependencyInfo dependency{
                .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier,
            };
            cmd.pipelineBarrier2(dependency);
        };
        auto descriptors = [&](vk::ImageView sr, vk::ImageView depth_view,
                               vk::ImageView motion_view) {
            const std::array<vk::DescriptorImageInfo, 8> infos{{
                {.imageView = sr, .imageLayout = vk::ImageLayout::eGeneral},
                {.sampler = *depth_sampler, .imageView = depth_view,
                 .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = motion_view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = color.view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = scene_depth.view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = mv.view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = *local_return_view, .imageLayout = vk::ImageLayout::eGeneral},
                {.imageView = output.view, .imageLayout = vk::ImageLayout::eGeneral},
            }};
            std::array<vk::WriteDescriptorSet, 8> writes{};
            for (u32 i = 0; i < writes.size(); ++i) {
                writes[i] = {.dstSet = descriptor_set, .dstBinding = i,
                    .descriptorCount = 1,
                    .descriptorType = i == 1 ? vk::DescriptorType::eCombinedImageSampler
                                              : vk::DescriptorType::eStorageImage,
                    .pImageInfo = &infos[i]};
            }
            device.updateDescriptorSets(writes, {});
        };
        transfer(color.image, VK_QUEUE_FAMILY_EXTERNAL, queue_family,
                 vk::PipelineStageFlagBits2::eAllCommands, {},
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderWrite);
        transfer(scene_depth.image, VK_QUEUE_FAMILY_EXTERNAL, queue_family,
                 vk::PipelineStageFlagBits2::eAllCommands, {},
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderWrite);
        transfer(mv.image, VK_QUEUE_FAMILY_EXTERNAL, queue_family,
                 vk::PipelineStageFlagBits2::eAllCommands, {},
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderWrite);

        // The game SR/depth/MV views are all in GENERAL at this point in Run().  This
        // dependency orders their last writes before the capture shader reads them.
        const std::array<vk::ImageMemoryBarrier2, 3> source_barriers{{
            {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
             .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
             .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = sr_color,
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
             .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
             .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
             .oldLayout = depth_layout, .newLayout = depth_layout,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = depth,
             .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}},
            {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
             .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
             .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = motion,
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
        }};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = source_barriers.size(),
                              .pImageMemoryBarriers = source_barriers.data()});
        descriptors(sr_view, depth_view, motion_view);
        const u32 capture_mode = 0;
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0,
                              descriptor_set, {});
        cmd.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                          sizeof(capture_mode), &capture_mode);
        cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);

        // Preserve a local GPU readback of the exact SR half pixels. It is compared with
        // the independent half return image before merge; the SR image itself stays intact.
        const vk::ImageMemoryBarrier2 fallback_to_copy{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = sr_color,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &fallback_to_copy});
        vk::BufferImageCopy fallback_copy{
            .bufferOffset = bytes32,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {width, height, 1},
        };
        cmd.copyImageToBuffer(sr_color, vk::ImageLayout::eGeneral,
                              readback->Handle(), fallback_copy);
        transfer(color.image, queue_family, VK_QUEUE_FAMILY_EXTERNAL,
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderWrite,
                 vk::PipelineStageFlagBits2::eBottomOfPipe, {});
        transfer(scene_depth.image, queue_family, VK_QUEUE_FAMILY_EXTERNAL,
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderWrite,
                 vk::PipelineStageFlagBits2::eBottomOfPipe, {});
        transfer(mv.image, queue_family, VK_QUEUE_FAMILY_EXTERNAL,
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderWrite,
                 vk::PipelineStageFlagBits2::eBottomOfPipe, {});
        const vk::BufferMemoryBarrier2 host_barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = readback->Handle(), .offset = bytes32, .size = bytes16,
        };
        cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1,
                              .pBufferMemoryBarriers = &host_barrier});

        input_tick = scheduler.CurrentTick();
        last_submitted_tick = input_tick;
        outstanding_tick = input_tick;
        scheduler.Flush();
        if (!WaitSubmitted(scheduler, input_tick))
            throw std::runtime_error("C1 SR capture submission exceeded 10s fence deadline");
        last_completed_tick = input_tick;
        outstanding_tick = 0;
        readback->Invalidate(bytes32, bytes16);
        cmd = scheduler.CommandBuffer();

        Json input = Envelope(session, "input", "INPUT_READY", 0, source_game_frame);
        input["hdr"] = true;
        input["exposure_valid"] = false;
        input["source_reset"] = source_reset;
        input["viewport_jitter_x"] = viewport_jitter[0];
        input["viewport_jitter_y"] = viewport_jitter[1];
        input["sr_jitter_x"] = sr_jitter[0];
        input["sr_jitter_y"] = sr_jitter[1];
        const auto input_path = run_dir + "/input-" + session + "-g0-f0.json";
        if (HasFile(input_path)) throw std::runtime_error("C1 refusing to overwrite input tuple");
        AtomicWrite(input_path, input.dump());
        std::printf("C1_INPUT_READY source_game_frame=%llu reset=%u viewport_jitter=%.9g,%.9g "
                    "sr_jitter=%.9g,%.9g hdr=1 exposure_valid=0 input_tick=%llu\n",
                    static_cast<unsigned long long>(source_game_frame), source_reset,
                    viewport_jitter[0], viewport_jitter[1], sr_jitter[0], sr_jitter[1],
                    static_cast<unsigned long long>(input_tick));

        const auto complete_path = run_dir + "/complete-" + session + "-g0-f0.json";
        const auto complete_text = WaitJson(complete_path, std::chrono::seconds(10));
        if (complete_text.empty()) throw std::runtime_error("C1 helper completion timeout; slot quarantined");
        const Json complete = Json::parse(complete_text);
        if (complete.at("session") != session || complete.at("generation") != 0 ||
            complete.at("frame") != 0 || complete.at("resource_set") != session + "-g0" ||
            complete.at("source_game_frame") != source_game_frame ||
            complete.at("profile_id") != "hdr-run-dlaa-1to1-v1" ||
            complete.at("width") != width || complete.at("height") != height ||
            complete.at("render_width") != width || complete.at("render_height") != height ||
            complete.at("op") != "complete" || complete.at("status") != "NOOP_VALID" ||
            complete.at("completed_GPU") != true || complete.at("output_valid") != true ||
            complete.at("model_valid") != false || complete.at("mode") != "noop" ||
            complete.at("copy_bitexact") != true || complete.at("evaluate_calls") != 0 ||
            complete.at("resolve_calls") != 0 || complete.at("output_digest_algorithm") !=
                "fnv1a64_rgba32f_bytes" || complete.at("output_digest_bytes") != bytes32)
            throw std::runtime_error("C1 helper complete tuple or no-op result invalid");
        const std::string expected_digest = complete.at("output_digest").get<std::string>();
        if (expected_digest.size() != 16 ||
            complete.at("source_digest").get<std::string>() != expected_digest)
            throw std::runtime_error("C1 helper digest malformed or source/output differ");

        // The helper has returned ownership only after its own GPU fence. Acquire the shared
        // output on the game queue, hash its exact local readback, and copy to an independent
        // half image that can safely outlive the shared import until merge completes.
        transfer(output.image, VK_QUEUE_FAMILY_EXTERNAL, queue_family,
                 vk::PipelineStageFlagBits2::eAllCommands, {},
                 vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eShaderRead);
        vk::BufferImageCopy digest_copy{
            .bufferOffset = 0,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {width, height, 1},
        };
        cmd.copyImageToBuffer(output.image, vk::ImageLayout::eGeneral,
                              readback->Handle(), digest_copy);
        const vk::ImageMemoryBarrier2 output_to_shader{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = output.image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &output_to_shader});
        const vk::ImageMemoryBarrier2 return_init{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {}, .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .oldLayout = vk::ImageLayout::eUndefined, .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk::Image(local_return),
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &return_init});
        const u32 return_mode = 1;
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0,
                              descriptor_set, {});
        cmd.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                          sizeof(return_mode), &return_mode);
        cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);
        const vk::ImageMemoryBarrier2 return_to_copy{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk::Image(local_return),
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &return_to_copy});
        vk::BufferImageCopy return_copy{
            .bufferOffset = bytes32 + bytes16,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {width, height, 1},
        };
        cmd.copyImageToBuffer(vk::Image(local_return), vk::ImageLayout::eGeneral,
                              readback->Handle(), return_copy);
        const vk::ImageMemoryBarrier2 return_ready{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk::Image(local_return),
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &return_ready});
        transfer(output.image, queue_family, VK_QUEUE_FAMILY_EXTERNAL,
                 vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eShaderRead,
                 vk::PipelineStageFlagBits2::eBottomOfPipe, {});
        const std::array<vk::BufferMemoryBarrier2, 2> readback_ready{{
            {.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
             .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eHost,
             .dstAccessMask = vk::AccessFlagBits2::eHostRead,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .buffer = readback->Handle(), .offset = 0, .size = bytes32},
            {.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
             .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eHost,
             .dstAccessMask = vk::AccessFlagBits2::eHostRead,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .buffer = readback->Handle(), .offset = bytes32, .size = bytes16 * 2},
        }};
        cmd.pipelineBarrier2({.bufferMemoryBarrierCount = readback_ready.size(),
                              .pBufferMemoryBarriers = readback_ready.data()});
        const u64 return_tick = scheduler.CurrentTick();
        last_submitted_tick = return_tick;
        outstanding_tick = return_tick;
        scheduler.Flush();
        if (!WaitSubmitted(scheduler, return_tick))
            throw std::runtime_error("C1 output acquire/readback fence exceeded 10s deadline");
        last_completed_tick = return_tick;
        outstanding_tick = 0;
        readback->Invalidate(0, bytes32 + bytes16 * 2);
        const auto* mapped = readback->mapped_data.data();
        const std::string actual_digest = Hex64(Fnv1a(mapped, bytes32));
        if (actual_digest != expected_digest)
            throw std::runtime_error("C1 helper/native shared-output FNV digest mismatch");
        if (std::memcmp(mapped + bytes32, mapped + bytes32 + bytes16, bytes16) != 0)
            throw std::runtime_error("C1 independent RGBA16F return differs from immutable SR fallback");
        if (!process.ExporterIdentityCurrent())
            throw std::runtime_error("C1 exporter identity changed before consumed publication");
        Json consumed = Envelope(session, "consumed", "CONSUMED", 0, source_game_frame);
        const auto consumed_path = run_dir + "/consumed-" + session + "-g0-f0.json";
        if (HasFile(consumed_path)) throw std::runtime_error("C1 refusing to overwrite consumed tuple");
        AtomicWrite(consumed_path, consumed.dump());
        pending_source_frame = source_game_frame;
        last_source_frame = source_game_frame;
        returned_view = *local_return_view;
        std::printf("C1_NOOP_FRAME PASS source_game_frame=%llu helper_digest=%s native_digest=%s "
                    "fallback_half_bitexact=1 output_view=independent tick_in=%llu tick_out=%llu\n",
                    static_cast<unsigned long long>(source_game_frame), expected_digest.c_str(),
                    actual_digest.c_str(), static_cast<unsigned long long>(input_tick),
                    static_cast<unsigned long long>(return_tick));
        return returned_view;
    } catch (const std::exception& e) {
        Quarantine(e.what());
        return VK_NULL_HANDLE;
    }
}

void C1GameBridge::DeferRetirement(const Instance& instance, Scheduler& scheduler) {
    if (!pending_source_frame || retirement_scheduled || quarantined || finished) return;
    retirement_scheduled = true;
    const auto device = instance.GetDevice();
    const auto source_frame = *pending_source_frame;
    const std::string run_dir_copy = run_dir;
    const std::string session_copy = session;
    const auto raw_fds = exporter_raw_fds;
    const auto exporter_fds_at_baseline = exporter_baseline_fds;
    const pid_t verified_exporter_pid = exporter_pid;
    scheduler.DeferOperation([this, device, source_frame, run_dir_copy, session_copy,
                              raw_fds, exporter_fds_at_baseline, verified_exporter_pid] {
        try {
            // This callback is tied to the merge submission tick: the local return image and
            // its view remain alive until the merge no longer references them.
            local_return_view.reset();
            local_return = VideoCore::HostImage{};
            returned_view = nullptr;
            for (auto& resource : resources) {
                if (resource.view) device.destroyImageView(resource.view);
                if (resource.image) device.destroyImage(resource.image);
                if (resource.memory) device.freeMemory(resource.memory);
            }
            resources.clear();
            auto release = Envelope(session_copy, "release", "NATIVE_RELEASED", 1, source_frame);
            AtomicWrite(run_dir_copy + "/native-released-" + session_copy + "-g0.json",
                        release.dump());
            pending_source_frame.reset();
            finished = true;
            std::thread([run_dir_copy, session_copy, source_frame, raw_fds,
                         exporter_fds_at_baseline,
                         verified_exporter_pid] {
                try {
                    const auto retired_text = WaitJson(
                        run_dir_copy + "/retired-" + session_copy + "-g0.json",
                        std::chrono::seconds(30));
                    if (retired_text.empty())
                        throw std::runtime_error("C1 helper retirement timeout");
                    const Json retired = Json::parse(retired_text);
                    ValidateEnvelope(retired, session_copy, "retired", "RETIRED", 1,
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
                            throw std::runtime_error("C1 raw export payload remains after retirement");
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
                    Json audit = Envelope(session_copy, "audit", "FD_AUDITED", 1, source_frame);
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
                    std::printf("C1_RELEASE PASS source_game_frame=%llu merge_tick_completed=1 "
                                "raw_export_payloads_remaining=0\n",
                                static_cast<unsigned long long>(source_frame));
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "C1_RELEASE_AUDIT BLOCKED reason=%s\n", e.what());
                }
            }).detach();
        } catch (const std::exception& e) {
            Quarantine(e.what());
        }
    });
}

} // namespace Vulkan
