// SPDX-License-Identifier: MIT
// Exercises the EXISTING bridge only. Synthetic SR test, no NR or new backend.
#include "../../gpu/dlss_bridge/bbport_dlss_bridge.h"
#include "vk_test_context.h"
#include <array>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <unistd.h>
static void need(bool value, const char *code) {
  if (!value)
    throw std::runtime_error(code);
}
static void log_message(int warning, const char *message) {
  std::printf("DLSS: %s%s\n", warning ? "warning: " : "", message);
}
static float halton(unsigned i, unsigned base) {
  float f = 1, r = 0;
  while (i) {
    f /= base;
    r += f * (i % base);
    i /= base;
  }
  return r - .5f;
}
static float half_value(uint16_t h) {
  int e = (h >> 10) & 31;
  float m = (h & 1023) / 1024.f;
  float v = e == 0    ? std::ldexp(m, -14)
            : e == 31 ? NAN
                      : std::ldexp(1 + m, e - 15);
  return h & 32768 ? -v : v;
}
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  try {
    need(argc == 4,
         "E_USAGE dlss-sr-smoke BRIDGE_LIBRARY NGX_DIRECTORY dlaa|quality");
    std::string mode = argv[3];
    need(mode == "dlaa" || mode == "quality", "E_MODE");
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
      throw std::runtime_error(std::string("E_BRIDGE_LOAD ") + dlerror());
    auto get = reinterpret_cast<BbDlssGetApiFn>(dlsym(library, "BbDlssGetApi"));
    need(get, "E_BRIDGE_SYMBOL");
    auto api = get();
    need(api && api->abi == BBPORT_DLSS_BRIDGE_ABI, "E_BRIDGE_ABI");
    auto data = std::filesystem::absolute("out/dlss-smoke-data");
    std::filesystem::create_directories(data);
    need(api->Configure(std::filesystem::absolute(argv[2]).wstring().c_str(),
                        data.wstring().c_str(), log_message),
         "E_CONFIGURE");
    uint32_t count = 0;
    const VkExtensionProperties *required = nullptr;
    need(api->InstanceExtensions(&count, &required), "E_INSTANCE_EXTENSIONS");
    std::vector<const char *> names;
    for (uint32_t i = 0; i < count; ++i)
      names.push_back(required[i].extensionName);
    VkTestContext v;
    v.create_instance(names);
    v.select();
    names.clear();
    need(api->DeviceExtensions(v.instance, v.physical, &count, &required),
         "E_DEVICE_EXTENSIONS");
    for (uint32_t i = 0; i < count; ++i)
      names.push_back(required[i].extensionName);
    v.create_device(names, true);
    need(api->Initialize(v.instance, v.physical, v.device,
                         vkGetInstanceProcAddr, vkGetDeviceProcAddr),
         "E_NGX_INITIALIZE");
    constexpr uint32_t ow = 960, oh = 540;
    uint32_t w = mode == "dlaa" ? ow : 640, h = mode == "dlaa" ? oh : 360;
    constexpr VkFormat formats[]{VK_FORMAT_R16G16B16A16_SFLOAT,
                                 VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16_SFLOAT,
                                 VK_FORMAT_R16G16B16A16_SFLOAT};
    TestImage images[4];
    BbDlssImage resources[4];
    for (unsigned i = 0; i < 4; ++i) {
      uint32_t iw = i == 3 ? ow : w, ih = i == 3 ? oh : h;
      VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      ci.imageType = VK_IMAGE_TYPE_2D;
      ci.format = formats[i];
      ci.extent = {iw, ih, 1};
      ci.mipLevels = ci.arrayLayers = 1;
      ci.samples = VK_SAMPLE_COUNT_1_BIT;
      ci.tiling = VK_IMAGE_TILING_OPTIMAL;
      ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_STORAGE_BIT;
      VKCHECK(vkCreateImage(v.device, &ci, nullptr, &images[i].image));
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(v.device, images[i].image, &r);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = r.size;
      ai.memoryTypeIndex =
          v.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VKCHECK(vkAllocateMemory(v.device, &ai, nullptr, &images[i].memory));
      VKCHECK(
          vkBindImageMemory(v.device, images[i].image, images[i].memory, 0));
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = images[i].image;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = formats[i];
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VKCHECK(vkCreateImageView(v.device, &vi, nullptr, &images[i].view));
      resources[i] = {images[i].image,
                      images[i].view,
                      vi.subresourceRange,
                      formats[i],
                      iw,
                      ih};
    }
    v.begin();
    BbDlssFeature feature{
        w, h, ow, oh, mode == "dlaa" ? 0 : 1, 0, 0, mode == "dlaa" ? 1 : 0};
    need(api->CreateFeature(v.cmd, &feature), "E_CREATE_FEATURE");
    v.submit();
    v.wait();
    Readback rb;
    rb.create(v, size_t(ow) * oh * 8);
    for (unsigned frame = 0; frame < 10; ++frame) {
      v.begin();
      for (unsigned i = 0; i < 4; ++i) {
        image_barrier(v, images[i].image,
                      frame ? VK_IMAGE_LAYOUT_GENERAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_GENERAL);
        VkClearColorValue c{};
        if (i == 0) {
          c.float32[0] = .25f;
          c.float32[1] = .5f;
          c.float32[2] = .75f;
          c.float32[3] = 1;
        }
        if (i == 1)
          c.float32[0] = .5f;
        vkCmdClearColorImage(v.cmd, images[i].image, VK_IMAGE_LAYOUT_GENERAL,
                             &c, 1, &resources[i].range);
        image_barrier(v, images[i].image, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL);
      }
      BbDlssEvaluate e{resources[0],
                       resources[1],
                       resources[2],
                       resources[3],
                       halton(frame + 1, 2),
                       halton(frame + 1, 3),
                       frame == 0 || frame == 5,
                       16.666667f,
                       0};
      need(api->Evaluate(v.cmd, &e), "E_EVALUATE");
      image_barrier(v, images[3].image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL);
      rb.copy(v, images[3].image, ow, oh);
      rb.host_barrier(v);
      v.submit();
      v.wait();
      auto pixels = static_cast<uint16_t *>(rb.mapped);
      double sum[3]{};
      for (size_t p = 0; p < size_t(ow) * oh; ++p)
        for (unsigned c = 0; c < 3; ++c) {
          float value = half_value(pixels[p * 4 + c]);
          need(std::isfinite(value), "E_OUTPUT_NONFINITE");
          sum[c] += value;
        }
      for (unsigned c = 0; c < 3; ++c)
        need(std::abs(sum[c] / (ow * oh) - .25 * (c + 1)) < .12,
             "E_OUTPUT_COLOR");
      std::printf(
          "DLSS_SMOKE frame=%u reset=%d output=finite mean=%.4f,%.4f,%.4f\n",
          frame + 1, e.reset, sum[0] / (ow * oh), sum[1] / (ow * oh),
          sum[2] / (ow * oh));
    }
    api->ReleaseFeature();
    api->Shutdown();
    rb.destroy(v);
    for (auto &image : images) {
      vkDestroyImageView(v.device, image.view, nullptr);
      vkDestroyImage(v.device, image.image, nullptr);
      vkFreeMemory(v.device, image.memory, nullptr);
    }
    v.destroy();
    dlclose(library);
    std::printf("DLSS_SMOKE PASS mode=%s synthetic_only=1 NR=NOT_IMPLEMENTED\n",
                mode.c_str());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "DLSS_SMOKE FAIL %s\n", e.what());
    std::fflush(nullptr);
    _exit(2);
  }
}
