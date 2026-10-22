// SPDX-License-Identifier: GPL-3.0-or-later
// Finite private GPU fixture. All input frames survive until feature Release.
#pragma once
#include <nvsdk_ngx_vk.h>
#include <cmath>
#include <memory>
struct FgFixture {
  static constexpr uint32_t W = 1920, H = 1080;
  struct Image {
    VkImage image{};
    VkImageView view{};
    VkDeviceMemory memory{};
    NVSDK_NGX_Resource_VK ngx{};
  };
  struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
    uint64_t size{};
    NVSDK_NGX_Resource_VK ngx{};
  };
  VkDevice device{};
  VkPhysicalDevice physical{};
  VkCommandBuffer command{};
  PFN_vkGetDeviceProcAddr gdpa{};
  VkPhysicalDeviceMemoryProperties properties{};
  std::vector<Image> images;
  std::vector<Buffer> buffers;
  bool safe = true;
  template <class T> T fn(const char *n) {
    auto p = reinterpret_cast<T>(gdpa(device, n));
    require(p, n);
    return p;
  }
  uint32_t type(uint32_t bits, uint32_t flags) {
    for (unsigned i = 0; i < properties.memoryTypeCount; ++i)
      if ((bits & (1u << i)) &&
          (properties.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw Stop(3, "fixture memory type");
  }
  FgFixture(VkDevice d, VkPhysicalDevice p, VkCommandBuffer c,
            PFN_vkGetDeviceProcAddr g,
            PFN_vkGetPhysicalDeviceMemoryProperties m)
      : device(d), physical(p), command(c), gdpa(g) {
    m(p, &properties);
  }
  Image image(VkFormat format, bool write, uint32_t width=W, uint32_t height=H) {
    images.emplace_back();
    Image& r = images.back(); // Own partial allocation too, if a later API fails.
    VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    c.imageType = VK_IMAGE_TYPE_2D;
    c.format = format;
    c.extent = {width, height, 1};
    c.mipLevels = c.arrayLayers = 1;
    c.samples = VK_SAMPLE_COUNT_1_BIT;
    c.tiling = VK_IMAGE_TILING_OPTIMAL;
    c.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    api(fn<PFN_vkCreateImage>("vkCreateImage")(device, &c, nullptr, &r.image),
        "fixture image");
    VkMemoryRequirements q{};
    fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(
        device, r.image, &q);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = q.size;
    a.memoryTypeIndex =
        type(q.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    api(fn<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &a, nullptr,
                                                     &r.memory),
        "fixture image memory");
    api(fn<PFN_vkBindImageMemory>("vkBindImageMemory")(device, r.image,
                                                       r.memory, 0),
        "fixture image bind");
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = r.image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = format;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    api(fn<PFN_vkCreateImageView>("vkCreateImageView")(device, &v, nullptr,
                                                       &r.view),
        "fixture view");
    r.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    r.ngx.ReadWrite = write;
    r.ngx.Resource.ImageViewInfo = {r.view, r.image, v.subresourceRange,
                                    format, width, height};
    return r;
  }
  Buffer buffer(uint64_t bytes) {
    buffers.emplace_back();
    Buffer& r = buffers.back();
    r.size = bytes;
    VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    c.size = bytes;
    c.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    api(fn<PFN_vkCreateBuffer>("vkCreateBuffer")(device, &c, nullptr,
                                                 &r.buffer),
        "fixture buffer");
    VkMemoryRequirements q{};
    fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(
        device, r.buffer, &q);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = q.size;
    a.memoryTypeIndex =
        type(q.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    api(fn<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &a, nullptr,
                                                     &r.memory),
        "fixture buffer memory");
    api(fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device, r.buffer,
                                                         r.memory, 0),
        "fixture buffer bind");
    api(fn<PFN_vkMapMemory>("vkMapMemory")(device, r.memory, 0, bytes, 0,
                                           &r.mapped),
        "fixture map");
    r.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_BUFFER;
    r.ngx.ReadWrite = true;
    r.ngx.Resource.BufferInfo = {r.buffer, unsigned(bytes)};
    return r;
  }
  void barrier(Image &r, VkImageLayout before, VkImageLayout after) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = before;
    b.newLayout = after;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.srcAccessMask =
        before == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0
            : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.image = r.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
        command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  void host(Buffer &b, bool write) {
    VkBufferMemoryBarrier m{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    m.buffer = b.buffer;
    m.offset = 0;
    m.size = b.size;
    m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    m.srcAccessMask =
        write ? VK_ACCESS_HOST_WRITE_BIT : VK_ACCESS_MEMORY_WRITE_BIT;
    m.dstAccessMask =
        write ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT;
    fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
        command,
        write ? VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        write ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_HOST_BIT, 0,
        0, nullptr, 1, &m, 0, nullptr);
  }
  void upload(Image &r, Buffer &b) {
    host(b, true);
    barrier(r, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {r.ngx.Resource.ImageViewInfo.Width,r.ngx.Resource.ImageViewInfo.Height,1};
    fn<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(
        command, b.buffer, r.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &c);
    barrier(r, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  void readback(Image &r, Buffer &b) {
    barrier(r, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {r.ngx.Resource.ImageViewInfo.Width,r.ngx.Resource.ImageViewInfo.Height,1};
    fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(
        command, r.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.buffer, 1,
        &c);
    host(b, false);
  }
  ~FgFixture() {
    if (!safe)
      return;
    for (auto &b : buffers) {
      if (b.mapped)
        fn<PFN_vkUnmapMemory>("vkUnmapMemory")(device, b.memory);
      fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device, b.buffer, nullptr);
      fn<PFN_vkFreeMemory>("vkFreeMemory")(device, b.memory, nullptr);
    }
    for (auto &r : images) {
      fn<PFN_vkDestroyImageView>("vkDestroyImageView")(device, r.view, nullptr);
      fn<PFN_vkDestroyImage>("vkDestroyImage")(device, r.image, nullptr);
      fn<PFN_vkFreeMemory>("vkFreeMemory")(device, r.memory, nullptr);
    }
  }
};
