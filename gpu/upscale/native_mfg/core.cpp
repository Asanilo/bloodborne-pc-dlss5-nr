#include "profile.hpp"
#include "profile1080.generated.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <openssl/evp.h>

namespace player_core {
namespace {

constexpr std::string_view sdk_sha = "aef4c911831b0381a6afa0164b743138e8571822646b6a4226e810a035ad678e";
constexpr VkImageSubresourceRange internal_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
constexpr size_t max_outputs = 5, max_arguments = 240;
static_assert(std::endian::native == std::endian::little);

void need(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}

std::span<const char> verified(std::span<const char> bytes, const Source &source) {
    need(source.offset <= bytes.size() && source.size <= bytes.size() - source.offset, "SDK slice outside its file");
    const auto slice = bytes.subspan(source.offset, source.size);
    std::array<unsigned char, 32> digest{};
    size_t length{};
    need(EVP_Q_digest(nullptr, "SHA256", nullptr, slice.data(), slice.size(), digest.data(), &length) == 1 &&
             length == digest.size() && source.sha.size() == 64, "SHA-256 unavailable");
    constexpr std::string_view hex = "0123456789abcdef";
    for (size_t i = 0; i < digest.size(); ++i)
        need(source.sha[i * 2] == hex[digest[i] >> 4] && source.sha[i * 2 + 1] == hex[digest[i] & 15],
             "official SDK data identity mismatch");
    return slice;
}

std::vector<char> sdk_bytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    const auto size = file.tellg();
    need(size > 0 && size < 16 * 1024 * 1024, "unexpected official SDK size");
    std::vector<char> bytes(static_cast<size_t>(size));
    file.seekg(0);
    need(bool(file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))), "could not read SDK");
    verified(bytes, {0, bytes.size(), sdk_sha});
    return bytes;
}

void memory_barrier(VkCommandBuffer command) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

template<class T> T get(VkDevice device, const char *name) {
    const auto ptr = vkGetDeviceProcAddr(device, name);
    need(ptr != nullptr, name);
    return reinterpret_cast<T>(ptr);
}

template<class Spec> size_t slots(std::span<const Spec> specs) {
    return 1 + std::ranges::max_element(specs, {}, &Spec::id)->id;
}

struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    VkDeviceAddress address{};
};

struct OwnedImage {
    VkImage handle{};
    VmaAllocation allocation{};
    ImageRole role{};
    const ImageSpec *spec{};
};

struct Texture {
    VkImageView view{};
    VkSampler sampler{};
    VkDescriptorType kind{};
    ImageRole role{};
    uint64_t internal{};
    std::array<uint64_t, max_outputs> current{};
};

struct Program {
    VkCuModuleNVX module{};
    VkCuFunctionNVX function{};
};

enum class Geometry : uint8_t { fixed, input_motion, prev_to_curr, curr_to_prev, interpolate };

Geometry geometry(std::string_view kernel) {
    if (kernel == "Kernel_InputMvecProcessing") return Geometry::input_motion;
    if (kernel == "Kernel_EstimatePrev2CurrScatter") return Geometry::prev_to_curr;
    if (kernel == "Kernel_Prev2CurrUnpackPull") return Geometry::curr_to_prev;
    if (kernel == "Kernel_EstimateIntermMvecsScatter") return Geometry::interpolate;
    return Geometry::fixed;
}

struct Prepared {
    alignas(8) std::array<std::byte, max_arguments> arguments{};
    const Dispatch *dispatch{};
    std::vector<Argument> patches;
    Geometry geometry{};
};

} // namespace

struct Core::Impl {
    VkDevice device{};
    VmaAllocator allocator;
    const Profile &profile;
    PFN_vkDestroyCuModuleNVX destroy_module{};
    PFN_vkDestroyCuFunctionNVX destroy_function{};
    PFN_vkCmdCuLaunchKernelNVX launch{};
    PFN_vkGetImageViewHandle64NVX image_handle{};
    std::vector<Buffer> buffers;
    std::vector<OwnedImage> images;
    std::vector<Texture> textures;
    std::vector<Program> programs;
    std::vector<Prepared> dispatches;
    std::array<const ImageSpec *, 5> roles{};
    Buffer upload;
    std::vector<std::pair<unsigned, VkBufferCopy>> copies;
    bool uploaded{}, seeded{};
    unsigned variant{};

    Impl(VmaAllocator allocator, const Profile &profile) : allocator(allocator), profile(profile) {
        VmaAllocatorInfo info{};
        vmaGetAllocatorInfo(allocator, &info);
        device = info.device;
    }

    ~Impl() {
        for (const auto &program : programs) {
            if (program.function) destroy_function(device, program.function, nullptr);
            if (program.module) destroy_module(device, program.module, nullptr);
        }
        for (const auto &texture : textures) {
            if (texture.view) vkDestroyImageView(device, texture.view, nullptr);
            if (texture.sampler) vkDestroySampler(device, texture.sampler, nullptr);
        }
        for (const auto &image : images) {
            if (image.handle) vmaDestroyImage(allocator, image.handle, image.allocation);
        }
        for (auto &buffer : buffers) destroy(buffer);
        destroy(upload);
    }

    void destroy(Buffer &buffer) noexcept {
        if (buffer.handle) vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
        buffer = {};
    }

    void create(Buffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        VmaAllocationCreateInfo allocation{};
        allocation.requiredFlags = memory_flags;
        check(vmaCreateBuffer(allocator, &info, &allocation, &buffer.handle, &buffer.allocation, nullptr),
              "vmaCreateBuffer");
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            VkBufferDeviceAddressInfo query{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            query.buffer = buffer.handle;
            buffer.address = vkGetBufferDeviceAddress(device, &query);
            need(buffer.address != 0, "Vulkan buffer has no device address");
        }
    }

    void create(const ImageSpec &spec) {
        auto &image = images.at(spec.id);
        image.role = spec.role;
        image.spec = &spec;
        if (spec.role != ImageRole::internal) {
            roles[static_cast<size_t>(spec.role)] = &spec;
            return;
        }
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = static_cast<VkFormat>(spec.format);
        info.extent = spec.extent;
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = spec.usage;
        VmaAllocationCreateInfo allocation{};
        allocation.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        check(vmaCreateImage(allocator, &info, &allocation, &image.handle, &image.allocation, nullptr),
              "vmaCreateImage");
    }

    uint64_t descriptor(const Texture &texture, VkImageView view) const {
        VkImageViewHandleInfoNVX info{VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX};
        info.imageView = view;
        info.descriptorType = texture.kind;
        info.sampler = texture.sampler;
        const uint64_t result = image_handle(device, &info);
        need(result != 0, "image view did not yield an NVX descriptor");
        return result;
    }

    void create(const TextureSpec &spec) {
        auto &texture = textures.at(spec.id);
        const auto &image = images.at(spec.image);
        texture.role = image.role;
        texture.kind = static_cast<VkDescriptorType>(spec.type);
        need(spec.handle_bits == 64, "expected the audited 64-bit graphics ABI");
        if (texture.kind == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            info.minFilter = static_cast<VkFilter>(spec.min_filter);
            info.magFilter = static_cast<VkFilter>(spec.mag_filter);
            info.mipmapMode = static_cast<VkSamplerMipmapMode>(spec.mip_mode);
            info.addressModeU = static_cast<VkSamplerAddressMode>(spec.address_modes[0]);
            info.addressModeV = static_cast<VkSamplerAddressMode>(spec.address_modes[1]);
            info.addressModeW = static_cast<VkSamplerAddressMode>(spec.address_modes[2]);
            info.minLod = spec.min_lod;
            info.maxLod = spec.max_lod;
            info.mipLodBias = spec.lod_bias;
            info.borderColor = static_cast<VkBorderColor>(spec.border);
            info.unnormalizedCoordinates = spec.unnormalized;
            check(vkCreateSampler(device, &info, nullptr, &texture.sampler), "vkCreateSampler");
        }
        if (image.role != ImageRole::internal) {
            need(std::ranges::all_of(spec.swizzle, [](uint32_t swizzle) { return swizzle == 0; }),
                 "external image view requires a swizzle");
            return;
        }
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = image.handle;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = static_cast<VkFormat>(spec.format);
        info.components = {static_cast<VkComponentSwizzle>(spec.swizzle[0]),
                           static_cast<VkComponentSwizzle>(spec.swizzle[1]),
                           static_cast<VkComponentSwizzle>(spec.swizzle[2]),
                           static_cast<VkComponentSwizzle>(spec.swizzle[3])};
        info.subresourceRange = internal_range;
        check(vkCreateImageView(device, &info, nullptr, &texture.view), "vkCreateImageView");
        texture.internal = descriptor(texture, texture.view);
    }

    void import(std::span<const char> sdk) {
        const auto create_module = get<PFN_vkCreateCuModuleNVX>(device, "vkCreateCuModuleNVX");
        const auto create_function = get<PFN_vkCreateCuFunctionNVX>(device, "vkCreateCuFunctionNVX");
        destroy_module = get<PFN_vkDestroyCuModuleNVX>(device, "vkDestroyCuModuleNVX");
        destroy_function = get<PFN_vkDestroyCuFunctionNVX>(device, "vkDestroyCuFunctionNVX");
        launch = get<PFN_vkCmdCuLaunchKernelNVX>(device, "vkCmdCuLaunchKernelNVX");
        image_handle = get<PFN_vkGetImageViewHandle64NVX>(device, "vkGetImageViewHandle64NVX");
        programs.resize(profile.programs.size());
        for (size_t index = 0; index < programs.size(); ++index) {
            const auto &spec = profile.programs[index];
            const auto bytes = verified(sdk, spec.data);
            auto &program = programs[index];
            VkCuModuleTexturingModeCreateInfoNVX mode{VK_STRUCTURE_TYPE_CU_MODULE_TEXTURING_MODE_CREATE_INFO_NVX};
            mode.use64bitTexturing = VK_TRUE;
            VkCuModuleCreateInfoNVX module{VK_STRUCTURE_TYPE_CU_MODULE_CREATE_INFO_NVX};
            module.pNext = spec.use_texture64 ? &mode : nullptr;
            module.dataSize = bytes.size();
            module.pData = bytes.data();
            check(create_module(device, &module, nullptr, &program.module), spec.name);
            VkCuFunctionCreateInfoNVX function{VK_STRUCTURE_TYPE_CU_FUNCTION_CREATE_INFO_NVX};
            function.module = program.module;
            function.pName = spec.name;
            check(create_function(device, &function, nullptr, &program.function), spec.name);
        }
    }

    static void write(Prepared &packed, size_t offset, uint64_t bits, size_t size) {
        std::memcpy(packed.arguments.data() + offset, &bits, size);
    }

    void prepare_arguments() {
        dispatches.resize(profile.dispatches.size());
        for (size_t index = 0; index < dispatches.size(); ++index) {
            const auto &spec = profile.dispatches[index];
            auto &packed = dispatches[index];
            need(spec.size <= max_arguments, "unsupported argument size");
            packed.dispatch = &spec;
            packed.geometry = geometry(profile.programs[spec.module].name);
            for (const auto &arg : spec.parameters) {
                need(arg.offset + arg.size <= spec.size, "out-of-range argument");
                switch (arg.kind) {
                case ArgumentKind::scalar:
                    write(packed, arg.offset, arg.bits, arg.size);
                    break;
                case ArgumentKind::address:
                    write(packed, arg.offset, buffers.at(arg.resource).address + arg.bits, arg.size);
                    break;
                case ArgumentKind::descriptor:
                    if (const auto &texture = textures.at(arg.resource); texture.role == ImageRole::internal)
                        write(packed, arg.offset, texture.internal, arg.size);
                    else
                        packed.patches.push_back(arg);
                    break;
                case ArgumentKind::phase:
                    packed.patches.push_back(arg);
                    break;
                }
            }
        }
    }

    void initialize(const std::filesystem::path &path) {
        auto sdk = sdk_bytes(path);
        import(sdk);
        size_t total = 0;
        buffers.resize(slots(profile.buffers));
        for (const auto &spec : profile.buffers) {
            create(buffers[spec.id], spec.size, spec.usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (spec.initial_data.size) {
                need(spec.initial_data.size == spec.size &&
                         (spec.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT), "unexpected weights destination");
                copies.emplace_back(spec.id, VkBufferCopy{total, 0, spec.initial_data.size});
                total += spec.initial_data.size;
            }
        }
        create(upload, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::vector<std::span<const char>> sources;
        sources.reserve(copies.size());
        for (const auto &spec : profile.buffers)
            if (spec.initial_data.size)
                sources.push_back(verified(sdk, spec.initial_data));
        void *mapped{};
        check(vmaMapMemory(allocator, upload.allocation, &mapped), "vmaMapMemory");
        for (size_t index = 0; index < sources.size(); ++index)
            std::memcpy(static_cast<char *>(mapped) + copies[index].second.srcOffset,
                        sources[index].data(), sources[index].size());
        vmaUnmapMemory(allocator, upload.allocation);
        images.resize(slots(profile.images));
        textures.resize(slots(profile.textures));
        for (const auto &spec : profile.images) create(spec);
        for (const auto &spec : profile.textures) create(spec);
        prepare_arguments();
    }

    void record_initialize(VkCommandBuffer command) {
        need(command && !uploaded, "invalid core initialization command");
        for (const auto &image : images) {
            if (!image.handle)
                continue;
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.handle;
            barrier.subresourceRange = internal_range;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        for (const auto &[destination, region] : copies)
            vkCmdCopyBuffer(command, upload.handle, buffers[destination].handle, 1, &region);
        memory_barrier(command);
        uploaded = true;
    }

    void verify_image(const Image &image, ImageRole role) const {
        const auto *expected = roles[static_cast<size_t>(role)];
        need(expected && image.image && image.view && image.range.levelCount == 1 && image.range.layerCount == 1 &&
                 image.width == expected->extent.width && image.height == expected->extent.height &&
                 static_cast<uint32_t>(image.format) == expected->format,
             "image does not match the fixed execution profile");
    }

    void bind(const Frame &frame, std::span<const Image> outputs) {
        const std::array<VkImageView, 4> inputs{VK_NULL_HANDLE, frame.color.view, frame.depth.view, frame.motion.view};
        for (const auto &spec : profile.textures) {
            auto &texture = textures[spec.id];
            if (texture.role == ImageRole::internal)
                continue;
            if (texture.role == ImageRole::result) {
                for (size_t index = 0; index < outputs.size(); ++index)
                    texture.current[index] = descriptor(texture, outputs[index].view);
            } else {
                texture.current[0] = descriptor(texture, inputs[static_cast<size_t>(texture.role)]);
            }
        }
    }

    static void geometry_args(std::array<std::byte, max_arguments> &arguments, Geometry type, const Frame &frame) {
        const auto &camera = frame.camera;
        const auto store = [&](size_t offset, auto value) { std::memcpy(arguments.data() + offset, &value, sizeof(value)); };
        switch (type) {
        case Geometry::fixed:
            break;
        case Geometry::input_motion:
            store(84, uint8_t(camera.depth_inverted));
            store(152, camera.motion_scale_x / float(frame.motion.width));
            store(156, camera.motion_scale_y / float(frame.motion.height));
            break;
        case Geometry::prev_to_curr:
            store(76, uint8_t(camera.depth_inverted));
            if (camera.has_matrices) store(80, camera.clip_to_prev_clip);
            break;
        case Geometry::curr_to_prev:
            store(156, uint8_t(camera.depth_inverted));
            if (camera.has_matrices) store(160, camera.prev_clip_to_clip);
            break;
        case Geometry::interpolate:
            store(136, uint8_t(camera.depth_inverted));
            break;
        }
    }

    void record(VkCommandBuffer command, const Sequence &sequence, const Frame &frame, float phase, size_t output) {
        size_t fill_index = 0, clear_index = 0;
        for (size_t index = 0; index < sequence.dispatches.size(); ++index) {
            while (fill_index < sequence.fills.size() && sequence.fills[fill_index].before == index) {
                const auto &fill = sequence.fills[fill_index++];
                vkCmdFillBuffer(command, buffers[fill.buffer].handle, fill.offset, fill.size, fill.value);
                memory_barrier(command);
            }
            while (clear_index < sequence.clears.size() && sequence.clears[clear_index].before == index) {
                const auto &clear = sequence.clears[clear_index++];
                VkClearColorValue value{};
                std::copy(clear.value.begin(), clear.value.end(), value.uint32);
                vkCmdClearColorImage(command, images[clear.image].handle, VK_IMAGE_LAYOUT_GENERAL, &value, 1,
                                     &internal_range);
                memory_barrier(command);
            }
            const auto &prepared = dispatches[sequence.dispatches[index]];
            const auto &step = *prepared.dispatch;
            alignas(8) std::array<std::byte, max_arguments> scratch;
            const auto *arguments = prepared.arguments.data();
            if (!prepared.patches.empty() || prepared.geometry != Geometry::fixed) {
                std::memcpy(scratch.data(), arguments, step.size);
                for (const auto &patch : prepared.patches) {
                    uint64_t bits = std::bit_cast<uint32_t>(phase);
                    if (patch.kind == ArgumentKind::descriptor) {
                        const auto &texture = textures[patch.resource];
                        bits = texture.current[texture.role == ImageRole::result ? output : 0];
                    }
                    std::memcpy(scratch.data() + patch.offset, &bits, patch.size);
                }
                geometry_args(scratch, prepared.geometry, frame);
                arguments = scratch.data();
            }
            const void *extras[]{reinterpret_cast<void *>(1), arguments, reinterpret_cast<void *>(2), &step.size, nullptr};
            VkCuLaunchInfoNVX info{VK_STRUCTURE_TYPE_CU_LAUNCH_INFO_NVX};
            info.function = programs[step.module].function;
            info.gridDimX = step.grid[0];
            info.gridDimY = step.grid[1];
            info.gridDimZ = step.grid[2];
            info.blockDimX = step.block[0];
            info.blockDimY = step.block[1];
            info.blockDimZ = step.block[2];
            info.sharedMemBytes = step.shared;
            info.extraCount = 1;
            info.pExtras = extras;
            launch(command, &info);
            memory_barrier(command);
        }
    }

    RecordResult frame(VkCommandBuffer command, const Frame &frame, std::span<const Image> outputs) {
        need(command && uploaded, "core resources have not been initialized on the caller queue");
        const bool reset = frame.reset || !seeded;
        verify_image(frame.color, ImageRole::color);
        verify_image(frame.depth, ImageRole::depth);
        verify_image(frame.motion, ImageRole::motion);
        if (reset) outputs = {};
        need(reset || (!outputs.empty() && outputs.size() <= max_outputs), "steady frames need one to five outputs");
        for (size_t i = 0; i < outputs.size(); ++i) {
            const auto &target = outputs[i];
            verify_image(target, ImageRole::result);
            need(target.image != frame.color.image && target.image != frame.depth.image && target.image != frame.motion.image
                     && std::ranges::none_of(outputs.first(i), [&](const auto &prior) { return prior.image == target.image; }),
                 "frame outputs must use distinct, independent images");
        }
        need(std::isfinite(frame.camera.motion_scale_x) && std::isfinite(frame.camera.motion_scale_y),
             "motion scales must be finite");
        bind(frame, outputs);
        memory_barrier(command);
        if (reset) {
            record(command, profile.seed, frame, .5f, 0);
            seeded = true;
            variant = 0;
            return RecordResult::seeded;
        }
        record(command, profile.pair[variant], frame, .5f, 0);
        for (size_t i = 0; i < outputs.size(); ++i)
            record(command, profile.phase[variant], frame, float(i + 1) / float(outputs.size() + 1), i);
        variant ^= 1;
        return RecordResult::generated;
    }
};

const Profile &profile_1080p_quality() noexcept { return stock_1080p_quality::definition; }

Core::Core() = default;
Core::~Core() = default;

bool Core::initialize(VmaAllocator allocator, const Profile &profile,
                      const std::filesystem::path &sdk_library, std::string &error) {
    try {
        need(allocator && !impl_, "missing caller allocator or already initialized");
        auto state = std::make_unique<Impl>(allocator, profile);
        state->initialize(sdk_library);
        impl_ = std::move(state);
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}

void Core::record_initialize(VkCommandBuffer command) {
    need(bool(impl_), "initialize core before recording its uploads");
    impl_->record_initialize(command);
}

void Core::release_upload() noexcept {
    if (impl_ && impl_->uploaded)
        impl_->destroy(impl_->upload);
}

RecordResult Core::record(VkCommandBuffer command, const Frame &frame, std::span<const Image> outputs,
                          std::string &error) {
    try {
        need(bool(impl_), "initialize core before recording source frames");
        const auto result = impl_->frame(command, frame, outputs);
        error.clear();
        return result;
    } catch (const std::exception &failure) {
        error = failure.what();
        return RecordResult::failed;
    }
}

} // namespace player_core
