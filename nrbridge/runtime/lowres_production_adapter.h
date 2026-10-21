#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// New per-image/per-dispatch lowres adapter from frozen Q2; production shader unchanged.
// Derived from pinned OptiScaler NR-only PoC vk_runtime.h. Production algorithm unchanged.
// Minimal owned, serialized Vulkan fixture. Existing shader/SPIR-V + forwarder only.
// No swapchain, SR, DualFeature, enlarger, FG, cache, debug or comparison path.
#include "../shaders/DlssNr_Common.h"
#include "DlssNr_Shader_Vk.h"
#include <cmath>
#include <limits>
#define V(name) api<PFN_##name>(#name)
class LowresProduction
{
  public:
    static constexpr unsigned W = 1920, H = 1080;
    struct Image
    {
        VkImage handle;
        VkDeviceMemory mem;
        VkImageView view;
        VkFormat format;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        NVSDK_NGX_Resource_VK ngx {};
    };
    struct Buffer
    {
        VkBuffer handle;
        VkDeviceMemory mem;
        VkDeviceSize bytes;
        void* mapped = nullptr;
    };
    VkDevice device;
    VkCommandBuffer command;
    VkQueue queue;
    PFN_vkGetDeviceProcAddr gdpa;
    VkPhysicalDeviceMemoryProperties memory {};
    std::vector<Image> images;
    std::vector<Buffer> buffers;
    VkDescriptorSetLayout setLayout = nullptr;
    VkPipelineLayout pipeLayout = nullptr;
    VkPipeline pipe = nullptr;
    VkDescriptorPool descriptorPool = nullptr;
    VkSampler sampler = nullptr;
    VkFence frameFence = nullptr;
    bool safe = true;
    uint64_t serial = 0;
    uint32_t gpu_timeout_ms = 10000;
    double fence_wait_cpu_ms = 0; // Measured vkWaitForFences call only, not End/QueueSubmit.
    template <class T> T api(const char* name)
    {
        auto f = (T) gdpa(device, name);
        need(f, name);
        return f;
    }
    uint32_t memtype(uint32_t mask, VkMemoryPropertyFlags flags)
    {
        for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
            if ((mask & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        throw std::runtime_error("memory type unavailable");
    }
    Image image(VkFormat format, bool writable, unsigned iw = W, unsigned ih = H)
    {
        Image r {};
        r.format = format;
        VkImageCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = { iw, ih, 1 };
        ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(V(vkCreateImage)(device, &ci, nullptr, &r.handle), "image");
        VkMemoryRequirements req {};
        V(vkGetImageMemoryRequirements)(device, r.handle, &req);
        VkMemoryAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(V(vkAllocateMemory)(device, &ai, nullptr, &r.mem), "image memory");
        check(V(vkBindImageMemory)(device, r.handle, r.mem, 0), "image bind");
        std::printf("PRIVATE_IMAGE_ALLOC format=%u extent=%ux%u allocation_bytes=%llu type=%u\n",format,iw,ih,(unsigned long long)req.size,ai.memoryTypeIndex);
        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = r.handle;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        check(V(vkCreateImageView)(device, &vi, nullptr, &r.view), "image view");
        r.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
        r.ngx.ReadWrite = writable;
        r.ngx.Resource.ImageViewInfo = { r.view, r.handle, vi.subresourceRange, format, iw, ih };
        images.push_back(r);
        return r;
    }
    Buffer buffer(VkDeviceSize bytes, VkBufferUsageFlags usage)
    {
        Buffer r {};
        r.bytes = bytes;
        VkBufferCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = bytes;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(V(vkCreateBuffer)(device, &ci, nullptr, &r.handle), "buffer");
        VkMemoryRequirements req {};
        V(vkGetBufferMemoryRequirements)(device, r.handle, &req);
        VkMemoryAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex =
            memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(V(vkAllocateMemory)(device, &ai, nullptr, &r.mem), "buffer memory");
        check(V(vkBindBufferMemory)(device, r.handle, r.mem, 0), "buffer bind");
        check(V(vkMapMemory)(device, r.mem, 0, bytes, 0, &r.mapped), "buffer map");
        std::printf("PRIVATE_BUFFER_ALLOC requested_bytes=%llu allocation_bytes=%llu type=%u local_only=1\n",(unsigned long long)bytes,(unsigned long long)req.size,ai.memoryTypeIndex);
        buffers.push_back(r);
        return r;
    }
    void transition(Image& i, VkImageLayout to)
    {
        VkImageMemoryBarrier b {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask =
            i.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b.oldLayout = i.layout;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = i.handle;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        V(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                                nullptr, 0, nullptr, 1, &b);
        i.layout = to;
    }
    void memoryBarrier()
    {
        VkMemoryBarrier b {};
        b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        V(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                                &b, 0, nullptr, 0, nullptr);
    }
    void clear(Image& i, float sentinel)
    {
        transition(i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue val {};
        for (int j = 0; j < 4; j++)
            val.float32[j] = sentinel;
        VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        V(vkCmdClearColorImage)(command, i.handle, i.layout, &val, 1, &range);
        transition(i, VK_IMAGE_LAYOUT_GENERAL);
    }
    void shader()
    {
        VkSamplerCreateInfo sm {};
        sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sm.magFilter = sm.minFilter = VK_FILTER_LINEAR;
        sm.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sm.addressModeU = sm.addressModeV = sm.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sm.maxLod = 1;
        check(V(vkCreateSampler)(device, &sm, nullptr, &sampler), "sampler");
        VkDescriptorSetLayoutBinding bindings[8] {};
        for (unsigned i = 0; i < 8; i++)
        {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType = i == 0  ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                         : i < 5 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                         : i < 7 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                 : VK_DESCRIPTOR_TYPE_SAMPLER;
        }
        VkDescriptorSetLayoutCreateInfo sl {};
        sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sl.bindingCount = 8;
        sl.pBindings = bindings;
        check(V(vkCreateDescriptorSetLayout)(device, &sl, nullptr, &setLayout), "descriptor layout");
        VkPipelineLayoutCreateInfo pl {};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        check(V(vkCreatePipelineLayout)(device, &pl, nullptr, &pipeLayout), "pipeline layout");
        std::vector<uint32_t> words(sizeof(dlssnr_spv) / 4);
        std::memcpy(words.data(), dlssnr_spv, sizeof(dlssnr_spv));
        VkShaderModuleCreateInfo sci {};
        sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        sci.codeSize = sizeof(dlssnr_spv);
        sci.pCode = words.data();
        VkShaderModule shader = nullptr;
        check(V(vkCreateShaderModule)(device, &sci, nullptr, &shader), "production SPIRV");
        VkComputePipelineCreateInfo pc {};
        pc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pc.layout = pipeLayout;
        pc.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pc.stage.module = shader;
        pc.stage.pName = "CSMain";
        check(V(vkCreateComputePipelines)(device, nullptr, 1, &pc, nullptr, &pipe), "production pipeline");
        V(vkDestroyShaderModule)(device, shader, nullptr);
        VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 },
                                         { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
                                         { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16 },
                                         { VK_DESCRIPTOR_TYPE_SAMPLER, 8 } };
        VkDescriptorPoolCreateInfo dp {};
        dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dp.maxSets = 8;
        dp.poolSizeCount = 4;
        dp.pPoolSizes = sizes;
        check(V(vkCreateDescriptorPool)(device, &dp, nullptr, &descriptorPool), "descriptor pool");
        VkFenceCreateInfo fi {};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(V(vkCreateFence)(device, &fi, nullptr, &frameFence), "frame fence");
    }
    void dispatch(DlssNrConstants constants, Image& source, Image& model, Image& original, Image& motion, Image& target,
                  Image& keep, Buffer* prepared = nullptr)
    {
        auto cb = prepared ? *prepared : buffer(sizeof(constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        std::memcpy(cb.mapped, &constants, sizeof(constants));
        VkDescriptorSet set = nullptr;
        VkDescriptorSetAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout;
        check(V(vkAllocateDescriptorSets)(device, &ai, &set), "fresh descriptor set");
        VkDescriptorBufferInfo bi { cb.handle, 0, sizeof(constants) };
        VkDescriptorImageInfo ii[7] {};
        Image* slots[] = { &source, &model, &original, &motion, &target, &keep };
        for (unsigned i = 0; i < 6; i++)
        {
            ii[i].imageView = slots[i]->view;
            ii[i].imageLayout = slots[i]->layout;
            ii[i].sampler = i < 4 ? sampler : nullptr;
        }
        ii[6].sampler = sampler;
        VkWriteDescriptorSet writes[8] {};
        for (unsigned i = 0; i < 8; i++)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 0  ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                       : i < 5 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                       : i < 7 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                               : VK_DESCRIPTOR_TYPE_SAMPLER;
            if (i == 0)
                writes[i].pBufferInfo = &bi;
            else
                writes[i].pImageInfo = &ii[i - 1];
        }
        V(vkUpdateDescriptorSets)(device, 8, writes, 0, nullptr);
        V(vkCmdBindPipeline)(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        V(vkCmdBindDescriptorSets)(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout, 0, 1, &set, 0, nullptr);
        need(constants.Width==target.ngx.Resource.ImageViewInfo.Width && constants.Height==target.ngx.Resource.ImageViewInfo.Height,"dispatch target/constant extent exact");
        V(vkCmdDispatch)(command, (constants.Width + 7) / 8, (constants.Height + 7) / 8, 1);
        memoryBarrier();
    }
    // Call only after prior fence, and after resetting the previous command buffer.
    void reset_frame_descriptors()
    {
        need(safe, "persistent descriptor reset requires prior actual GPU fence");
        check(V(vkResetDescriptorPool)(device, descriptorPool, 0), "production frame descriptor reset");
    }
    Buffer read(Image& i, bool fp16, Buffer* prepared = nullptr)
    {
        const auto& v=i.ngx.Resource.ImageViewInfo;
        const VkDeviceSize bytes=VkDeviceSize(v.Width)*v.Height*4*(fp16?2:4);
        auto b = prepared ? *prepared : buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        need(b.bytes==bytes,"actual per-image readback size");
        transition(i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { v.Width, v.Height, 1 };
        V(vkCmdCopyImageToBuffer)(command, i.handle, i.layout, b.handle, 1, &copy);
        return b;
    }
    void create_noop_fence() {
        VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(V(vkCreateFence)(device,&ci,nullptr,&frameFence),"noop frame fence");
    }
    void read_guide(Image& i, Buffer& b) {
        const auto& v=i.ngx.Resource.ImageViewInfo;
        const uint64_t expected=uint64_t(v.Width)*v.Height*(i.format==VK_FORMAT_R32_SFLOAT?4u:8u);
        need(b.bytes==expected,"actual guide readback buffer extent/format");
        transition(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent={v.Width,v.Height,1};
        V(vkCmdCopyImageToBuffer)(command,i.handle,i.layout,b.handle,1,&copy);
    }
    void submit()
    {
        need(serial<9007199254740991ull,"GPU serial exhausted before submit");
        VkMemoryBarrier b {};
        b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        V(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &b, 0,
                                nullptr, 0, nullptr);
        check(V(vkEndCommandBuffer)(command), "frame end");
        check(V(vkResetFences)(device, 1, &frameFence), "fence reset");
        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &command;
        safe = false;
        check(V(vkQueueSubmit)(queue, 1, &si, frameFence), "frame submit");
        const auto wait_start_ms = c4_cpu_now_ms();
        auto wait = V(vkWaitForFences)(device, 1, &frameFence, VK_TRUE, uint64_t(gpu_timeout_ms) * 1000000ull);
        fence_wait_cpu_ms = c4_cpu_now_ms() - wait_start_ms;
        std::printf("NR_GPU_WAIT result=%d deadline_ms=%u actual_fence=1 CPU_wait_call_ms=%.6f\n", int(wait), gpu_timeout_ms, fence_wait_cpu_ms);
        if (wait != VK_SUCCESS) throw Stop(wait == VK_TIMEOUT ? 3 : 2, "NR GPU fence incomplete; HOLD resources");
        check(V(vkGetFenceStatus)(device, frameFence), "frame fence observed");
        safe = true;
        need(serial<9007199254740991ull,"GPU serial exhausted");++serial;
        std::printf("GPU_COMPLETE value=%llu observed=%llu wait=0 route=WineVulkan\n",
                    (unsigned long long)serial, (unsigned long long)serial);
    }
    static float unhalf(uint16_t h)
    {
        float s = (h & 0x8000) ? -1.f : 1.f;
        int e = (h >> 10) & 31, m = h & 1023;
        return e == 31  ? (m ? NAN : s * INFINITY)
               : e == 0 ? s * std::ldexp(float(m), -24)
                        : s * std::ldexp(1.f + float(m) / 1024, e - 15);
    }
    bool inspect(Buffer b, bool fp16, bool negative, const char* label, const std::string& path)
    {
        size_t finite = 0, written = 0, total = W * H * 3, alphaFinite = 0;
        float lo = INFINITY, hi = -INFINITY;
        FILE* dump = std::fopen(path.c_str(), "wb");
        need(dump, "output dump open");
        for (unsigned p = 0; p < W * H; p++)
            for (unsigned c = 0; c < 4; c++)
            {
                float v = fp16 ? unhalf(((uint16_t*) b.mapped)[p * 4 + c]) : ((float*) b.mapped)[p * 4 + c];
                need(std::fwrite(&v, 4, 1, dump) == 1, "output dump write");
                if (c < 3)
                {
                    if (std::isfinite(v))
                    {
                        finite++;
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
                    if (fp16 ? std::isfinite(v) : v != -1234.f)
                        written++;
                }
                else if (std::isfinite(v))
                    alphaFinite++;
            }
        std::fclose(dump);
        std::printf("READBACK %s finite=%zu written=%zu total=%zu alpha_finite=%zu min=%g max=%g negative=%d\n", label,
                    finite, written, total, alphaFinite, lo, hi, int(negative));
        return negative ? written == 0 : finite == total && written == total && (fp16 || alphaFinite == W * H);
    }

  public:
    LowresProduction(VkInstance i, VkPhysicalDevice p, VkDevice d, VkCommandBuffer c, VkQueue q, PFN_vkGetInstanceProcAddr g)
        : device(d), command(c), queue(q)
    {
        gdpa = proc<PFN_vkGetDeviceProcAddr>(g, i, "vkGetDeviceProcAddr");
        proc<PFN_vkGetPhysicalDeviceMemoryProperties>(g, i, "vkGetPhysicalDeviceMemoryProperties")(p, &memory);
    }
    ~LowresProduction()
    {
        if (!safe)
            return;
        for (auto& b : buffers)
        {
            if (b.mapped)
                V(vkUnmapMemory)(device, b.mem);
            V(vkDestroyBuffer)(device, b.handle, nullptr);
            V(vkFreeMemory)(device, b.mem, nullptr);
        }
        for (auto& i : images)
        {
            V(vkDestroyImageView)(device, i.view, nullptr);
            V(vkDestroyImage)(device, i.handle, nullptr);
            V(vkFreeMemory)(device, i.mem, nullptr);
        }
        if (frameFence)
            V(vkDestroyFence)(device, frameFence, nullptr);
        if (descriptorPool)
            V(vkDestroyDescriptorPool)(device, descriptorPool, nullptr);
        if (pipe)
            V(vkDestroyPipeline)(device, pipe, nullptr);
        if (pipeLayout)
            V(vkDestroyPipelineLayout)(device, pipeLayout, nullptr);
        if (setLayout)
            V(vkDestroyDescriptorSetLayout)(device, setLayout, nullptr);
        if (sampler)
            V(vkDestroySampler)(device, sampler, nullptr);
    }
    void upload(Image& i, Buffer& b, const void* data, size_t bytes) {
        const auto& info=i.ngx.Resource.ImageViewInfo;
        const uint64_t stride=i.format==VK_FORMAT_R32_SFLOAT?4u:i.format==VK_FORMAT_R32G32_SFLOAT?8u:16u;
        need(bytes==uint64_t(info.Width)*info.Height*stride && b.bytes==bytes,"exact local upload extent/bytes");
        std::memcpy(b.mapped,data,bytes);
        VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        host.buffer=b.handle;host.offset=0;host.size=bytes;
        api<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&host,0,nullptr);
        transition(i,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={info.Width,info.Height,1};
        api<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(command,b.handle,i.handle,i.layout,1,&copy);
        memoryBarrier();
    }
};
#undef V
