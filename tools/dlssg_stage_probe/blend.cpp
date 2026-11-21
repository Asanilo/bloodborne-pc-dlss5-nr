// Run only the stock DLSS-G color candidate stage on self-owned Vulkan resources.
#include "vk_test_context.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <dlfcn.h>
#include <fstream>
#include <filesystem>
#include <memory>
#include <numeric>
#include <span>
#include <string_view>

namespace {
constexpr uint32_t width = 64, height = 32, tile = 16, threads = 256;
constexpr uint32_t pixels = width * height;
constexpr int left_a = 12, left_b = 36, stripe_width = 8, top = 6, bottom = 26;
constexpr VkFormat rgba = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat rg = VK_FORMAT_R32G32_SFLOAT;
constexpr VkFormat scalar = VK_FORMAT_R32_SFLOAT;
constexpr auto colors = std::to_array<float>({.75f, .25f, .125f});
constexpr auto background = std::to_array<float>({.125f, .0625f, .03125f});
using Pixel = std::array<float, 4>;
constexpr Pixel aux = {.25f, .5f, .0f, .0f};

// One by-value kernel parameter. Unused fields hold zero.
struct alignas(8) Params {
    uint64_t color_a, color_b, auxiliary;
    VkDeviceAddress motion_b, motion_a;
    uint64_t confidence, image_a, image_b, image_mask;
    VkDeviceAddress packed_a, packed_b, packed_mask;
    std::array<std::byte, 88> unused_a;
    uint32_t padded_width, padded_height, scale, motion_width, motion_height;
    std::array<std::byte, 8> unused_b;
    uint32_t input_width, input_height;
    uint8_t hud_enabled;
    std::array<std::byte, 3> padding;
    uint64_t hud_a, hud_b;
};
static_assert(sizeof(Params) == 240 && alignof(Params) == 8);
static_assert(offsetof(Params, motion_b) == 24 && offsetof(Params, confidence) == 40);
static_assert(offsetof(Params, image_a) == 48 && offsetof(Params, packed_a) == 72);
static_assert(offsetof(Params, padded_width) == 184 && offsetof(Params, scale) == 192);
static_assert(offsetof(Params, motion_width) == 196 && offsetof(Params, input_width) == 212);
static_assert(offsetof(Params, hud_enabled) == 220 && offsetof(Params, hud_a) == 224);
struct alignas(8) WeightParams {
    uint32_t logit_width, logit_height, output_width, output_height;
    uint64_t logit_a, logit_b;
    VkDeviceAddress motion_b, motion_a;
    uint64_t output;
    std::array<std::byte, 32> unused;
};
static_assert(sizeof(WeightParams) == 88 && alignof(WeightParams) == 8);
static_assert(offsetof(WeightParams, logit_a) == 16 && offsetof(WeightParams, motion_b) == 32 &&
              offsetof(WeightParams, output) == 48);
static_assert(std::endian::native == std::endian::little && sizeof(_Float16) == 2);

void require(bool condition, const char *why) {
    if (!condition)
        throw std::runtime_error(why);
}

uint16_t half_bits(float f) {
    return std::bit_cast<uint16_t>(static_cast<_Float16>(f));
}

Pixel endpoint(int left, int x, int y, float alpha) {
    const auto &rgb = x >= left && x < left + stripe_width && y >= top && y < bottom
                          ? colors : background;
    return {rgb[0], rgb[1], rgb[2], alpha};
}

using Logits = std::array<float, 2>;

Logits endpoint_logits(size_t endpoint, int x, int y) {
    if (y == top)
        return {0.f, 0.f};
    if (y > top && y < bottom)
        return {32.f, 0.f};
    const bool right = x >= int(width) / 2;
    return {bool(endpoint) != right ? 1.f : -1.f, right ? -.25f : .25f};
}

float expected_weight(size_t endpoint, int x, int warped_x, int y) {
    const float base = endpoint_logits(endpoint, x, y)[1];
    const float warped = endpoint_logits(endpoint, std::clamp(warped_x, 0, int(width) - 1), y)[0];
    return float(1. / (1. + std::exp(double(base - warped))));
}

constexpr VkImageUsageFlags sampled_use = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr VkImageUsageFlags storage_use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

struct GpuImage {
    TestImage handles;

    void create(VkTestContext &v, VkFormat format, VkImageUsageFlags usage) {
        VkImageFormatProperties supported{};
        VKCHECK(vkGetPhysicalDeviceImageFormatProperties(
            v.physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported));
        require(supported.maxExtent.width >= width && supported.maxExtent.height >= height,
                "image extent unavailable");
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        VKCHECK(vkCreateImage(v.device, &info, nullptr, &handles.image));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(v.device, handles.image, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = v.memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VKCHECK(vkAllocateMemory(v.device, &alloc, nullptr, &handles.memory));
        VKCHECK(vkBindImageMemory(v.device, handles.image, handles.memory, 0));

        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = handles.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VKCHECK(vkCreateImageView(v.device, &view, nullptr, &handles.view));
    }

    void destroy(VkDevice device) {
        if (handles.view) vkDestroyImageView(device, handles.view, nullptr);
        if (handles.image) vkDestroyImage(device, handles.image, nullptr);
        if (handles.memory) vkFreeMemory(device, handles.memory, nullptr);
    }
};

struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
    VkDeviceAddress address{};

    void create(VkTestContext &v, VkDeviceSize bytes, VkBufferUsageFlags usage, bool bda) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes;
        info.usage = usage;
        VKCHECK(vkCreateBuffer(v.device, &info, nullptr, &buffer));
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(v.device, buffer, &req);
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.pNext = bda ? &flags : nullptr;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = v.memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VKCHECK(vkAllocateMemory(v.device, &alloc, nullptr, &memory));
        VKCHECK(vkBindBufferMemory(v.device, buffer, memory, 0));
        VKCHECK(vkMapMemory(v.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        if (bda) {
            VkBufferDeviceAddressInfo query{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            query.buffer = buffer;
            address = vkGetBufferDeviceAddress(v.device, &query);
            require(address && address % alignof(uint64_t) == 0, "invalid Vulkan buffer device address");
        }
    }

    void destroy(VkDevice device) {
        if (mapped) vkUnmapMemory(device, memory);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
    }
};

void device_barrier(VkTestContext &v, VkPipelineStageFlags source,
                    VkPipelineStageFlags destination, VkAccessFlags src, VkAccessFlags dst) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = src;
    barrier.dstAccessMask = dst;
    vkCmdPipelineBarrier(v.cmd, source, destination, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

uint32_t extension_revision(VkPhysicalDevice physical, std::string_view wanted) {
    uint32_t count = 0;
    VKCHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> extensions(count);
    VKCHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()));
    for (const auto &extension : extensions)
        if (wanted == extension.extensionName)
            return extension.specVersion;
    return 0;
}

void verify_gpu(VkTestContext &v) {
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(v.physical, &properties);
    require(driver.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY, "fixture requires NVIDIA Vulkan driver");
    require(v.properties.apiVersion >= VK_API_VERSION_1_2, "fixture requires Vulkan 1.2");
    std::printf("VULKAN_DRIVER %s %s\n", driver.driverName, driver.driverInfo);

    // Query architecture for the same device UUID. No CUDA context or allocations.
    const auto unload = [](void *handle) { dlclose(handle); };
    const std::unique_ptr<void, decltype(unload)> cuda(dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL),
                                                      unload);
    require(bool(cuda), "libcuda unavailable for Ada device verification");
    const auto init = reinterpret_cast<int (*)(unsigned)>(dlsym(cuda.get(), "cuInit"));
    const auto count = reinterpret_cast<int (*)(int *)>(dlsym(cuda.get(), "cuDeviceGetCount"));
    const auto device = reinterpret_cast<int (*)(int *, int)>(dlsym(cuda.get(), "cuDeviceGet"));
    using Uuid = std::array<unsigned char, VK_UUID_SIZE>;
    const auto uuid = reinterpret_cast<int (*)(Uuid *, int)>(dlsym(cuda.get(), "cuDeviceGetUuid"));
    const auto attr = reinterpret_cast<int (*)(int *, int, int)>(dlsym(cuda.get(), "cuDeviceGetAttribute"));
    require(init && count && device && uuid && attr, "CUDA device query symbols unavailable");
    int devices{};
    require(init(0) == 0 && count(&devices) == 0, "CUDA device enumeration failed");
    for (int i = 0; i < devices; ++i) {
        int dev{};
        Uuid id{};
        require(device(&dev, i) == 0 && uuid(&id, dev) == 0, "CUDA device UUID query failed");
        if (!std::equal(id.begin(), id.end(), v.ids.deviceUUID))
            continue;
        constexpr int compute_major = 75, compute_minor = 76;
        int major{}, minor{};
        require(attr(&major, compute_major, dev) == 0 && attr(&minor, compute_minor, dev) == 0,
                "CUDA architecture query failed");
        require(major == 8 && minor == 9, "stock sm89 cubin requires Ada compute capability 8.9");
        std::printf("CUDA_ARCH %d.%d matched Vulkan UUID\n", major, minor);
        return;
    }
    throw std::runtime_error("no CUDA device matches Vulkan UUID");
}

constexpr size_t rgba_size = pixels * sizeof(Pixel);
constexpr size_t scalar_size = pixels * sizeof(float);
constexpr size_t motion_size = pixels * sizeof(uint64_t);
constexpr size_t rgba_half_size = pixels * 4 * sizeof(uint16_t);
constexpr size_t scalar_half_size = pixels * sizeof(uint16_t);
constexpr size_t guard = 256;

constexpr size_t motion_b = guard;
constexpr size_t motion_a = motion_b + motion_size + guard;
constexpr size_t packed_a = motion_a + motion_size + guard;
constexpr size_t packed_b = packed_a + rgba_half_size + guard;
constexpr size_t packed_mask = packed_b + rgba_half_size + guard;
constexpr size_t data_size = packed_mask + scalar_half_size + guard;
constexpr auto ranges = std::to_array<std::pair<size_t, size_t>>({
    {motion_b, motion_size}, {motion_a, motion_size}, {packed_a, rgba_half_size},
    {packed_b, rgba_half_size}, {packed_mask, scalar_half_size}});

struct Case {
    const char *name;
    const char *file_phase;
    int shift;
    bool warp;
};

constexpr auto cases = std::to_array<Case>({
    {"0-confidence", "", 8, false}, {"1/3", "1-3", 8, true},
    {"1/2", "1-2", 12, true}, {"2/3", "2-3", 16, true}});
constexpr uint64_t missing = 0x7fffffff7fffffffull;
using Motion = std::array<uint64_t, pixels>;
using ScatterFields = std::array<Motion, 2>;
static_assert(sizeof(Motion) == motion_size);

uint32_t motion_bits(int x) {
    return uint32_t{half_bits(float(x) / width)} << 16;
}

ScatterFields read_scatter(const std::filesystem::path &directory, const Case &test) {
    ScatterFields fields;
    constexpr auto names = std::to_array({"previous", "current"});
    const std::array<uint32_t, 2> expected = {
        motion_bits(-test.shift), motion_bits(left_b - left_a - test.shift)};
    for (size_t j = 0; j < fields.size(); ++j) {
        const auto file = directory / (std::string("phase-") + test.file_phase + "-" + names[j] + ".bin");
        std::ifstream input(file, std::ios::binary);
        require(bool(input.read(reinterpret_cast<char *>(fields[j].data()), motion_size)) && input.peek() == EOF,
                ("incorrect scatter file size or unavailable file " + file.string()).c_str());
        unsigned absent = 0;
        for (uint32_t i = 0; i < pixels; ++i) {
            const auto word = fields[j][i];
            const uint32_t x = i % width, y = i / width;
            if (word == missing) {
                require(!(x >= 18 && x <= 38 && y >= 6 && y <= 25),
                        ("missing scatter motion in stripe test region " + file.string()).c_str());
                ++absent;
            } else {
                // The two fp16 zero encodings have the same displacement. Preserve the capture.
                require((uint32_t(word) & ~0x00008000u) == expected[j],
                        ("unexpected scatter motion vector " + file.string()).c_str());
            }
        }
        std::printf("SCATTER phase=%s source=%s absent=%u valid=%u\n",
                    test.name, names[j], absent, pixels - absent);
    }
    return fields;
}

struct Fixture {
    VkTestContext v;
    std::array<GpuImage, 4> inputs;
    std::array<GpuImage, 3> outputs;
    std::array<GpuImage, 2> logits;
    GpuImage weights;
    Buffer transfer, data;
    VkSampler sampler{};
    PFN_vkGetImageViewHandle64NVX handle{};
    PFN_vkDestroyCuModuleNVX destroy_module{};
    PFN_vkDestroyCuFunctionNVX destroy_function{};
    PFN_vkCmdCuLaunchKernelNVX launch{};
    VkCuModuleNVX module{}, weight_module{};
    VkCuFunctionNVX function{}, weight_function{};
    Params base{};
    WeightParams weight_params{};
    uint64_t weight_sample{};
    bool weight_initialized{};

    ~Fixture() {
        if (weight_function) destroy_function(v.device, weight_function, nullptr);
        if (weight_module) destroy_module(v.device, weight_module, nullptr);
        if (function) destroy_function(v.device, function, nullptr);
        if (module) destroy_module(v.device, module, nullptr);
        if (sampler) vkDestroySampler(v.device, sampler, nullptr);
        weights.destroy(v.device);
        for (auto &image : logits) image.destroy(v.device);
        for (auto &image : outputs) image.destroy(v.device);
        for (auto &image : inputs) image.destroy(v.device);
        data.destroy(v.device);
        transfer.destroy(v.device);
        v.destroy();
    }

    uint64_t image_handle(const GpuImage &image, bool sampled) {
        VkImageViewHandleInfoNVX view{VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX};
        view.imageView = image.handles.view;
        view.descriptorType = sampled ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        view.sampler = sampled ? sampler : VK_NULL_HANDLE;
        const uint64_t result = handle(v.device, &view);
        require(result != 0, "Vulkan image descriptor handle was zero");
        return result;
    }

    void init(std::span<const char> cubin, std::span<const char> weight_cubin) {
        v.create_instance();
        v.select();
        verify_gpu(v);
        const auto binary = extension_revision(v.physical, VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
        const auto handles = extension_revision(v.physical, VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
        std::printf("VULKAN_EXTENSIONS binary_import=%u image_view_handle=%u\n", binary, handles);
        require(binary >= 2 && handles >= 3, "64-bit Vulkan binary import extensions unavailable");
        v.create_device({VK_NVX_BINARY_IMPORT_EXTENSION_NAME, VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME}, true);
        handle = v.proc<PFN_vkGetImageViewHandle64NVX>("vkGetImageViewHandle64NVX");
        const auto create_module = v.proc<PFN_vkCreateCuModuleNVX>("vkCreateCuModuleNVX");
        const auto create_function = v.proc<PFN_vkCreateCuFunctionNVX>("vkCreateCuFunctionNVX");
        destroy_module = v.proc<PFN_vkDestroyCuModuleNVX>("vkDestroyCuModuleNVX");
        destroy_function = v.proc<PFN_vkDestroyCuFunctionNVX>("vkDestroyCuFunctionNVX");
        launch = v.proc<PFN_vkCmdCuLaunchKernelNVX>("vkCmdCuLaunchKernelNVX");

        const auto import = [&](std::span<const char> bytes, const char *name,
                                VkCuModuleNVX &mod, VkCuFunctionNVX &func) {
            VkCuModuleTexturingModeCreateInfoNVX mode{VK_STRUCTURE_TYPE_CU_MODULE_TEXTURING_MODE_CREATE_INFO_NVX};
            mode.use64bitTexturing = VK_TRUE;
            VkCuModuleCreateInfoNVX info{VK_STRUCTURE_TYPE_CU_MODULE_CREATE_INFO_NVX};
            info.pNext = &mode;
            info.dataSize = bytes.size();
            info.pData = bytes.data();
            VKCHECK(create_module(v.device, &info, nullptr, &mod));
            VkCuFunctionCreateInfoNVX entry{VK_STRUCTURE_TYPE_CU_FUNCTION_CREATE_INFO_NVX};
            entry.module = mod;
            entry.pName = name;
            VKCHECK(create_function(v.device, &entry, nullptr, &func));
        };
        import(cubin, "Kernel_BlendCandidatesFused", module, function);
        if (!weight_cubin.empty())
            import(weight_cubin, "Kernel_WarpBlendingWeights", weight_module, weight_function);

        for (auto &image : inputs) image.create(v, rgba, sampled_use);
        outputs[0].create(v, rgba, storage_use);
        outputs[1].create(v, rgba, storage_use);
        outputs[2].create(v, scalar, storage_use);
        transfer.create(v, (weight_function ? 6 : 4) * rgba_size,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        data.create(v, data_size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true);
        VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampling.magFilter = sampling.minFilter = VK_FILTER_NEAREST;
        sampling.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampling.addressModeU = sampling.addressModeV = sampling.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VKCHECK(vkCreateSampler(v.device, &sampling, nullptr, &sampler));

        base.color_a = image_handle(inputs[0], true);
        base.color_b = image_handle(inputs[1], true);
        base.auxiliary = image_handle(inputs[2], true);
        base.confidence = image_handle(inputs[3], true);
        base.image_a = image_handle(outputs[0], false);
        base.image_b = image_handle(outputs[1], false);
        base.image_mask = image_handle(outputs[2], false);
        base.motion_b = data.address + motion_b;
        base.motion_a = data.address + motion_a;
        base.packed_a = data.address + packed_a;
        base.packed_b = data.address + packed_b;
        base.packed_mask = data.address + packed_mask;
        base.padded_width = base.motion_width = base.input_width = width;
        base.padded_height = base.motion_height = base.input_height = height;
        base.scale = 1;
        if (weight_function) {
            for (auto &image : logits) image.create(v, rg, sampled_use);
            weights.create(v, rg, sampled_use | storage_use);
            weight_params.logit_width = weight_params.output_width = width;
            weight_params.logit_height = weight_params.output_height = height;
            weight_params.logit_a = image_handle(logits[0], true);
            weight_params.logit_b = image_handle(logits[1], true);
            weight_params.motion_a = base.motion_a;
            weight_params.motion_b = base.motion_b;
            weight_params.output = image_handle(weights, false);
            weight_sample = image_handle(weights, true);
        }
        std::printf("PREFLIGHT stock cubins=%d 64-bit texture and surface handles 5 BDA ranges\n",
                    weight_function ? 2 : 1);
    }

    void upload(const Case &test, const ScatterFields *fields) {
        auto *input = static_cast<Pixel *>(transfer.mapped);
        const float weight = test.warp ? 1.f : 0.f;
        for (uint32_t i = 0; i < pixels; ++i) {
            input[i] = endpoint(left_a, i % width, i / width, 1.f);
            input[i + pixels] = endpoint(left_b, i % width, i / width, 1.f);
            input[i + 2 * pixels] = aux;
            input[i + 3 * pixels] = {weight, weight, 0.f, 0.f};
        }
        if (weight_function && test.warp) {
            auto *bytes = static_cast<std::byte *>(transfer.mapped);
            for (size_t j = 0; j < logits.size(); ++j) {
                auto *samples = reinterpret_cast<Logits *>(bytes + (4 + j) * rgba_size);
                for (uint32_t i = 0; i < pixels; ++i)
                    samples[i] = endpoint_logits(j, i % width, i / width);
            }
        }
        std::memset(data.mapped, 0xcd, data_size);
        auto *bytes = static_cast<std::byte *>(data.mapped);
        if (fields) {
            std::memcpy(bytes + motion_a, (*fields)[0].data(), motion_size);
            std::memcpy(bytes + motion_b, (*fields)[1].data(), motion_size);
        } else {
            std::fill_n(reinterpret_cast<uint64_t *>(bytes + motion_a), pixels, motion_bits(-test.shift));
            std::fill_n(reinterpret_cast<uint64_t *>(bytes + motion_b), pixels,
                        motion_bits(left_b - left_a - test.shift));
        }
    }

    void upload_image(const GpuImage &image, VkDeviceSize offset, VkImageLayout prior) {
        image_barrier(v, image.handles.image, prior, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(v.cmd, transfer.buffer, image.handles.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(v, image.handles.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void copy_image(const GpuImage &image, VkDeviceSize offset) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(v.cmd, image.handles.image, VK_IMAGE_LAYOUT_GENERAL, transfer.buffer, 1, &region);
    }

    void all_memory_barrier() {
        device_barrier(v, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                       VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }

    void run_kernel(VkCuFunctionNVX kernel, const void *argument, size_t argument_size,
                    uint32_t gx, uint32_t gy, uint32_t bx) {
        // The pinned host uses NVX's packed-argument extra list, with extraCount=1.
        // The tags are CU_LAUNCH_PARAM_BUFFER_POINTER, BUFFER_SIZE, and END.
        const void *extra[] = {reinterpret_cast<void *>(1), argument,
                               reinterpret_cast<void *>(2), &argument_size, nullptr};
        VkCuLaunchInfoNVX cmd{VK_STRUCTURE_TYPE_CU_LAUNCH_INFO_NVX};
        cmd.function = kernel;
        cmd.gridDimX = gx;
        cmd.gridDimY = gy;
        cmd.gridDimZ = 1;
        cmd.blockDimX = bx;
        cmd.blockDimY = cmd.blockDimZ = 1;
        cmd.extraCount = 1;
        cmd.pExtras = extra;
        launch(v.cmd, &cmd);
    }

    void record(const Case &test, VkImageLayout prior) {
        const bool use_weights = weight_function && test.warp;
        v.begin();
        device_barrier(v, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        for (size_t i = 0; i < inputs.size(); ++i)
            upload_image(inputs[i], i * rgba_size, prior);
        if (use_weights) {
            const auto layout = weight_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
            for (size_t j = 0; j < logits.size(); ++j)
                upload_image(logits[j], (4 + j) * rgba_size, layout);
            image_barrier(v, weights.handles.image, layout, VK_IMAGE_LAYOUT_GENERAL);
            weight_initialized = true;
        }
        constexpr VkImageSubresourceRange subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue fill{};
        std::fill(std::begin(fill.float32), std::end(fill.float32), -31.f);
        for (auto &output : outputs) {
            image_barrier(v, output.handles.image, prior, VK_IMAGE_LAYOUT_GENERAL);
            vkCmdClearColorImage(v.cmd, output.handles.image, VK_IMAGE_LAYOUT_GENERAL, &fill, 1, &subresource);
        }
        if (use_weights)
            vkCmdClearColorImage(v.cmd, weights.handles.image, VK_IMAGE_LAYOUT_GENERAL, &fill, 1, &subresource);
        all_memory_barrier();

        Params params = base;
        if (use_weights) {
            std::printf("SUBMIT phase=%s stock_weights grid=1x%u block=%u\n", test.name, height, width);
            run_kernel(weight_function, &weight_params, sizeof(weight_params), 1, height, width);
            all_memory_barrier();
            params.confidence = weight_sample;
        }
        std::printf("SUBMIT phase=%s A_dx=%d B_dx=%d weights=%s\n", test.name, -test.shift,
                    left_b - left_a - test.shift, use_weights ? "stock" : (test.warp ? "1" : "0"));
        run_kernel(function, &params, sizeof(params), width / tile, height / tile, threads);

        device_barrier(v, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        for (size_t i = 0; i < outputs.size(); ++i)
            copy_image(outputs[i], i * rgba_size);
        if (use_weights)
            copy_image(weights, 3 * rgba_size);
        device_barrier(v, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        v.submit();
        const auto status = vkWaitForFences(v.device, 1, &v.fence, VK_TRUE, 3000000000ull);
        if (status != VK_SUCCESS) {
            std::fprintf(stderr, "FENCE phase=%s error=%d\n", test.name, int(status));
            std::_Exit(3); // resources may still be in use
        }
    }

    void write_ppm(size_t candidate, const std::filesystem::path &file) const {
        std::ofstream out(file, std::ios::binary);
        out << "P6\n" << width << ' ' << height << "\n255\n";
        const auto *bytes = static_cast<const std::byte *>(transfer.mapped) + candidate * rgba_size;
        for (uint32_t i = 0; i < pixels; ++i) {
            Pixel sample;
            std::memcpy(&sample, bytes + i * sizeof(Pixel), sizeof(sample));
            const std::array<unsigned char, 3> rgb = {
                static_cast<unsigned char>(std::lround(std::clamp(sample[0], 0.f, 1.f) * 255.f)),
                static_cast<unsigned char>(std::lround(std::clamp(sample[1], 0.f, 1.f) * 255.f)),
                static_cast<unsigned char>(std::lround(std::clamp(sample[2], 0.f, 1.f) * 255.f))};
            out.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
        }
        out.close();
        require(bool(out), "PPM readback write failed");
    }

    bool verify(const Case &test, const ScatterFields *fields) {
        std::array<unsigned, 3> bad_image{}, bad_packed{};
        std::array<unsigned, 2> seen{}, x_sum{}, bad_weights{};
        std::array<float, 2> max_weight_error{};
        const bool use_weights = weight_function && test.warp;
        const auto *bytes = static_cast<const std::byte *>(data.mapped);
        const auto *buffer = static_cast<const std::byte *>(transfer.mapped);
        constexpr std::array<int, 2> left = {left_a, left_b};
        const std::array<int, 2> shift = {-test.shift, left_b - left_a - test.shift};
        for (uint32_t i = 0; i < pixels; ++i) {
            const int x = i % width, y = i / width;
            const std::array<bool, 2> valid = {
                !fields || (*fields)[0][i] != missing, !fields || (*fields)[1][i] != missing};
            Logits measured_weight;
            if (use_weights)
                std::memcpy(&measured_weight, buffer + 3 * rgba_size + i * sizeof(Logits), sizeof(Logits));
            std::array<float, 2> weight{};
            for (size_t j = 0; j < 2; ++j) {
                const int warped_x = x + (valid[j] ? shift[j] : 0);
                weight[j] = use_weights ? expected_weight(j, x, warped_x, y) : float(test.warp);
                if (use_weights) {
                    const float error = std::abs(measured_weight[j] - weight[j]);
                    max_weight_error[j] = std::max(max_weight_error[j], error);
                    bad_weights[j] += !(error <= .000002f);
                }

                const size_t source = !valid[j] ? 1 - j : j;
                const int source_x = x + (valid[source] ? shift[source] : 0);
                const auto reference = endpoint(left[j], x, y, aux[j]);
                const auto sampled = endpoint(left[source], source_x, y, aux[j]);
                Pixel expected;
                for (size_t channel = 0; channel < expected.size(); ++channel)
                    expected[channel] = std::fma(weight[j], sampled[channel] - reference[channel], reference[channel]);
                Pixel actual;
                std::array<uint16_t, 4> packed;
                std::memcpy(&actual, buffer + j * rgba_size + i * sizeof(Pixel), sizeof(actual));
                std::memcpy(&packed, bytes + (j ? packed_b : packed_a) + i * sizeof(packed), sizeof(packed));
                bad_image[j] += actual != expected;
                bad_packed[j] += !std::equal(expected.begin(), expected.end(), packed.begin(),
                                             [](float f, uint16_t h) { return half_bits(f) == h; });
                if (actual[0] == colors[0]) {
                    ++seen[j];
                    x_sum[j] += x;
                }
            }
            float mask;
            uint16_t mask_half;
            std::memcpy(&mask, buffer + 2 * rgba_size + i * sizeof(mask), sizeof(mask));
            std::memcpy(&mask_half, bytes + packed_mask + i * sizeof(mask_half), sizeof(mask_half));
            const float expected_mask = !valid[0] && !valid[1] && std::max(weight[0], weight[1]) >= .5f ? 1.f : 0.f;
            bad_image[2] += mask != expected_mask;
            bad_packed[2] += mask_half != half_bits(expected_mask);
        }
        unsigned bad_guard = 0;
        size_t end = 0;
        for (const auto &[offset, size] : ranges) {
            bad_guard += std::count_if(bytes + end, bytes + offset,
                                       [](std::byte b) { return b != std::byte{0xcd}; });
            end = offset + size;
        }
        bad_guard += std::count_if(bytes + end, bytes + data_size,
                                   [](std::byte b) { return b != std::byte{0xcd}; });
        std::printf("RESULT phase=%s A_center=%.6f B_center=%.6f A_red=%u B_red=%u "
                    "image_errors=%u,%u,%u packed_errors=%u,%u,%u guard_errors=%u\n",
                    test.name, seen[0] ? double(x_sum[0]) / seen[0] : -1.,
                    seen[1] ? double(x_sum[1]) / seen[1] : -1., seen[0], seen[1],
                    bad_image[0], bad_image[1], bad_image[2], bad_packed[0], bad_packed[1],
                    bad_packed[2], bad_guard);
        if (use_weights)
            std::printf("WEIGHT_RESULT phase=%s A_errors=%u B_errors=%u max_error_A=%.8g max_error_B=%.8g\n",
                        test.name, bad_weights[0], bad_weights[1], max_weight_error[0], max_weight_error[1]);
        return bad_guard == 0 && std::accumulate(bad_image.begin(), bad_image.end(), 0u) == 0 &&
               std::accumulate(bad_packed.begin(), bad_packed.end(), 0u) == 0 &&
               std::accumulate(bad_weights.begin(), bad_weights.end(), 0u) == 0;
    }
};

template <size_t Size>
std::array<char, Size> read_cubin(const char *filename) {
    std::ifstream input(filename, std::ios::binary);
    std::array<char, Size> cubin;
    require(bool(input.read(cubin.data(), cubin.size())) && input.peek() == EOF &&
            std::memcmp(cubin.data(), "\x7f" "ELF\x02\x01\x01\x33\x07", 9) == 0 &&
            std::memcmp(cubin.data() + 48, "\x59\x05\x59\x00", 4) == 0,
            "unexpected cubin header or size");
    return cubin;
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool launch = argc >= 2 && std::string_view(argv[1]) == "--launch";
    const bool preflight = argc >= 2 && std::string_view(argv[1]) == "--preflight";
    if ((!launch && !preflight) || argc < 3 || argc > 5 || (preflight && argc > 4)) {
        std::fprintf(stderr, "usage: %s --preflight blend.sm89.cubin [weight.sm89.cubin]\n"
                             "       %s --launch blend.sm89.cubin [scatter-dir [weight.sm89.cubin]]\n",
                     argv[0], argv[0]);
        return 2;
    }
    try {
        const auto cubin = read_cubin<16800>(argv[2]);
        const bool use_weights = preflight ? argc == 4 : argc == 5;
        const auto weight_cubin = use_weights ? read_cubin<4136>(argv[argc - 1]) : std::array<char, 4136>{};

        const bool use_scatter = launch && argc >= 4;
        std::array<ScatterFields, cases.size()> scatter;
        if (use_scatter) {
            for (size_t i = 1; i < cases.size(); ++i)
                scatter[i] = read_scatter(argv[3], cases[i]);
        }

        const bool save_rgb = launch && std::getenv("BB_MFG_FIXTURE_RGB");
        std::filesystem::path rgb_directory;
        if (save_rgb) {
            rgb_directory = std::getenv("BB_MFG_FIXTURE_RGB");
            const auto status = std::filesystem::status(rgb_directory);
            const auto public_access = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
            require(std::filesystem::is_directory(status) &&
                        (status.permissions() & public_access) == std::filesystem::perms::none,
                    "BB_MFG_FIXTURE_RGB must name an existing private directory");
        }

        Fixture fixture;
        fixture.init(cubin, use_weights ? std::span<const char>(weight_cubin) : std::span<const char>{});
        if (preflight)
            return 0;
        auto layout = VK_IMAGE_LAYOUT_UNDEFINED;
        for (size_t i = 0; i < cases.size(); ++i) {
            const auto &test = cases[i];
            const auto *fields = use_scatter && test.warp ? &scatter[i] : nullptr;
            fixture.upload(test, fields);
            fixture.record(test, layout);
            if (!fixture.verify(test, fields))
                return 4;
            if (save_rgb && !test.warp) {
                fixture.write_ppm(0, rgb_directory / "endpoint-a.ppm");
                fixture.write_ppm(1, rgb_directory / "endpoint-b.ppm");
            }
            if (save_rgb && (std::string_view(test.file_phase) == "1-3" ||
                             std::string_view(test.file_phase) == "2-3"))
                fixture.write_ppm(0, rgb_directory / (std::string("phase-") + test.file_phase + ".ppm"));
            layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        std::puts("PASS stock candidate images and packed outputs match every expected pixel");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
