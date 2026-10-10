// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <vulkan/vulkan.h>

inline void vkcheck(VkResult result, const char *operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string("E_VULKAN ") + operation +
                             " VkResult=" + std::to_string(result));
}
#define VKCHECK(expr) vkcheck((expr), #expr)
struct VkTestContext {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  uint32_t family{};
  VkPhysicalDeviceMemoryProperties memory{};
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceIDProperties ids{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkCommandPool pool{};
  VkCommandBuffer cmd{};
  VkFence fence{};
  void create_instance(const std::vector<const char *> &extensions = {}) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    const char *validation = std::getenv("VK_INSTANCE_LAYERS");
    const char *layer = "VK_LAYER_KHRONOS_validation";
    if (validation && std::strstr(validation, layer)) {
      ci.enabledLayerCount = 1;
      ci.ppEnabledLayerNames = &layer;
    }
    VKCHECK(vkCreateInstance(&ci, nullptr, &instance));
    if (ci.enabledLayerCount)
      std::puts("VULKAN validation=enabled");
  }
  void select(const uint8_t *uuid = nullptr) {
    uint32_t n = 0;
    VKCHECK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    std::vector<VkPhysicalDevice> devices(n);
    VKCHECK(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    for (auto p : devices) {
      VkPhysicalDeviceIDProperties id{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
      VkPhysicalDeviceProperties2 prop{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
      prop.pNext = &id;
      vkGetPhysicalDeviceProperties2(p, &prop);
      if (uuid ? std::memcmp(uuid, id.deviceUUID, VK_UUID_SIZE) == 0
               : prop.properties.vendorID == 0x10de) {
        physical = p;
        properties = prop.properties;
        ids = id;
        ids.pNext = nullptr;
        break;
      }
    }
    if (!physical)
      throw std::runtime_error(uuid ? "E_DEVICE_UUID" : "E_NVIDIA_DEVICE");
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> queues(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qn, queues.data());
    bool found = false;
    for (uint32_t q = 0; q < qn; ++q)
      if (queues[q].queueCount &&
          (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        family = q;
        found = true;
        break;
      }
    if (!found)
      throw std::runtime_error("E_QUEUE_FAMILY");
    std::printf("GPU pid=%d name=%s driver=%u uuid=", int(getpid()),
                properties.deviceName, properties.driverVersion);
    for (auto c : ids.deviceUUID)
      std::printf("%02x", c);
    std::printf(" queue=%u\n", family);
  }
  void create_device(const std::vector<const char *> &extensions,
                     bool ngx = false) {
    uint32_t n = 0;
    VKCHECK(
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr));
    std::vector<VkExtensionProperties> avail(n);
    VKCHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n,
                                                 avail.data()));
    for (auto e : extensions)
      if (std::none_of(avail.begin(), avail.end(), [&](auto &p) {
            return std::strcmp(p.extensionName, e) == 0;
          }))
        throw std::runtime_error(std::string("E_EXTENSION ") + e);
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    VkPhysicalDeviceVulkan12Features supported12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 supported{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported.pNext = &supported12;
    vkGetPhysicalDeviceFeatures2(physical, &supported);
    VkPhysicalDeviceVulkan12Features enabled12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures enabled{};
    if (ngx) {
      if (!supported12.bufferDeviceAddress ||
          !supported.features.shaderStorageImageWriteWithoutFormat)
        throw std::runtime_error("E_NGX_DEVICE_FEATURES");
      enabled12.bufferDeviceAddress = VK_TRUE;
      enabled12.shaderFloat16 = supported12.shaderFloat16;
      enabled.shaderStorageImageWriteWithoutFormat = VK_TRUE;
      enabled.shaderInt16 = supported.features.shaderInt16;
      ci.pNext = &enabled12;
      ci.pEnabledFeatures = &enabled;
    }
    VKCHECK(vkCreateDevice(physical, &ci, nullptr, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VKCHECK(vkCreateCommandPool(device, &pi, nullptr, &pool));
    VkCommandBufferAllocateInfo ai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VKCHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VKCHECK(vkCreateFence(device, &fi, nullptr, &fence));
  }
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
      if ((bits & (1u << i)) &&
          (memory.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw std::runtime_error("E_MEMORY_TYPE");
  }
  template <class T> T proc(const char *name) {
    auto p = vkGetDeviceProcAddr(device, name);
    if (!p)
      throw std::runtime_error(std::string("E_PROC ") + name);
    return reinterpret_cast<T>(p);
  }
  void begin() {
    VKCHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VKCHECK(vkBeginCommandBuffer(cmd, &b));
  }
  void submit(VkSemaphore wait = {}, VkSemaphore signal = {}) {
    VKCHECK(vkEndCommandBuffer(cmd));
    VKCHECK(vkResetFences(device, 1, &fence));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.waitSemaphoreCount = wait ? 1 : 0;
    si.pWaitSemaphores = &wait;
    si.pWaitDstStageMask = &stage;
    si.signalSemaphoreCount = signal ? 1 : 0;
    si.pSignalSemaphores = &signal;
    VKCHECK(vkQueueSubmit(queue, 1, &si, fence));
  }
  void wait() {
    VKCHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 3000000000ull));
  }
  void destroy() {
    if (device) {
      vkDestroyFence(device, fence, nullptr);
      vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
      device = {};
    }
    if (instance) {
      vkDestroyInstance(instance, nullptr);
      instance = {};
    }
  }
};
struct TestImage {
  VkImage image{};
  VkDeviceMemory memory{};
  VkImageView view{};
};
inline void image_barrier(VkTestContext &v, VkImage image,
                          VkImageLayout old_layout, VkImageLayout layout,
                          uint32_t src = VK_QUEUE_FAMILY_IGNORED,
                          uint32_t dst = VK_QUEUE_FAMILY_IGNORED) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.image = image;
  b.oldLayout = old_layout;
  b.newLayout = layout;
  b.srcQueueFamilyIndex = src;
  b.dstQueueFamilyIndex = dst;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.srcAccessMask =
      old_layout == VK_IMAGE_LAYOUT_UNDEFINED || src == VK_QUEUE_FAMILY_EXTERNAL
          ? 0
          : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  b.dstAccessMask =
      dst == VK_QUEUE_FAMILY_EXTERNAL
          ? 0
          : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  vkCmdPipelineBarrier(v.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &b);
}
struct Readback {
  VkBuffer buffer{};
  VkDeviceMemory memory{};
  void *mapped{};
  VkDeviceSize size{};
  void create(VkTestContext &v, VkDeviceSize bytes) {
    size = bytes;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = bytes;
    ci.usage =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VKCHECK(vkCreateBuffer(v.device, &ci, nullptr, &buffer));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(v.device, buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = v.memory_type(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VKCHECK(vkAllocateMemory(v.device, &ai, nullptr, &memory));
    VKCHECK(vkBindBufferMemory(v.device, buffer, memory, 0));
    VKCHECK(vkMapMemory(v.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
  }
  void copy(VkTestContext &v, VkImage image, uint32_t width, uint32_t height,
            VkDeviceSize offset = 0) {
    VkBufferImageCopy c{};
    c.bufferOffset = offset;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(v.cmd, image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1,
                           &c);
  }
  void host_barrier(VkTestContext &v) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(v.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &b, 0, nullptr, 0,
                         nullptr);
  }
  void destroy(VkTestContext &v) {
    vkUnmapMemory(v.device, memory);
    vkDestroyBuffer(v.device, buffer, nullptr);
    vkFreeMemory(v.device, memory, nullptr);
  }
};
