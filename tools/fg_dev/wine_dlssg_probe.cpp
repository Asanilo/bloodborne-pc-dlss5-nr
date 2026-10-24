// SPDX-License-Identifier: GPL-3.0-or-later
// Finite offscreen entry probe; a successful Create is NOT generated-frame
// evidence.
#define api fg_verbose_api
#include "fixture_util.h"
#undef api
static void api(VkResult result,const char* stage){if(result!=VK_SUCCESS||!getenv("FG_POC_STREAM"))fg_verbose_api(result,stage);}
#define NGX_SNIPPET_BUILD
#include "dlssg_fixture.h"
#include <algorithm>
#include <nvsdk_ngx_defs_dlssg.h>
#include <nvsdk_ngx_vk.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include "dlssg_mfg_unlock.h"
#include "../../gpu/upscale/dlssg_layout.h"

template <class T> static T sym(HMODULE m, const char *n) {
  auto f = reinterpret_cast<T>(GetProcAddress(m, n));
  require(f, n);
  return f;
}
static std::wstring wide(const std::string &s) { return {s.begin(), s.end()}; }
static std::string win(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return "Z:" + s;
}
static void __cdecl model_log(const char *message, int level, int source) {
  if(getenv("FG_POC_STREAM")&&level>1)return;
  printf("FG_MODEL_LOG level=%d source=%d %s\n", level, source,
         message ? message : "<null>");
}
static double cpu_ms(){LARGE_INTEGER ticks,frequency;require(QueryPerformanceCounter(&ticks)&&QueryPerformanceFrequency(&frequency),"high-resolution CPU clock");return double(ticks.QuadPart)*1000./double(frequency.QuadPart);}
struct Probe {
  HMODULE cuda{}, bridge{}, loader{}, core{}, nvapi{}, dxgi{};
  bool snippet = false, model_attempted = false;
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  PFN_vkGetInstanceProcAddr gipa{};
  PFN_vkGetDeviceProcAddr gdpa{};
  uint32_t family{};
  VkCommandPool pool{};
  VkCommandBuffer command{};
  VkFence fence{};
  NVSDK_NGX_Parameter *params{};
  NVSDK_NGX_Handle *feature{};
  std::unique_ptr<FgFixture> fixture;
  std::shared_ptr<void> input_descriptors;
  nlohmann::json stream_terminal;
  bool core_attempted = false, inflight = false, shared_active = false;
  unsigned create_result = 0, generated_count = 1;
  DlssgGpu::Layout feature_layout;
  template <class T> T i(const char *n) {
    return reinterpret_cast<T>(gipa(instance, n));
  }
  template <class T> T d(const char *n) {
    return reinterpret_cast<T>(gdpa(device, n));
  }
  void init(const char *run, const char *ge, const char *adapter) {
    snippet = adapter != nullptr;
    if(const char* value=getenv("FG_POC_LAYOUT")){auto j=nlohmann::json::parse(value);feature_layout=DlssgGpu::Layout::From(j);}
    feature_layout.Validate();
    if(const char* count=getenv("FG_POC_GENERATED_COUNT"))generated_count=std::stoul(count);
    require(generated_count>=1&&generated_count<=5,"bounded MFG output count");
    std::string root = ge;
    dxgi = LoadLibraryExW(
        wide(win(root + "/lib/wine/dxvk/x86_64-windows/dxgi.dll")).c_str(),
        nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    require(dxgi, "DXGI load");
    nvapi = LoadLibraryExW(
        wide(win(root + "/lib/wine/nvapi/x86_64-windows/nvapi64.dll")).c_str(),
        nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    require(nvapi, "NVAPI load");
    core = LoadLibraryW(L"Z:\\usr\\lib\\nvidia\\wine\\_nvngx.dll");
    require(core, "PE core");
    loader = LoadLibraryW(L"vulkan-1.dll");
    require(loader, "Wine Vulkan");
    gipa = sym<PFN_vkGetInstanceProcAddr>(loader, "vkGetInstanceProcAddr");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "bbport M2 offscreen DLSSG";
    app.apiVersion = VK_API_VERSION_1_3;
    const char *ext = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = &ext;
    api(i<PFN_vkCreateInstance>("vkCreateInstance")(&ci, nullptr, &instance),
        "instance");
    uint32_t n = 0;
    api(i<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(
            instance, &n, nullptr),
        "device count");
    std::vector<VkPhysicalDevice> ps(n);
    api(i<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(
            instance, &n, ps.data()),
        "devices");
    for (auto p : ps) {
      VkPhysicalDeviceProperties pr{};
      i<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(
          p, &pr);
      if (pr.vendorID == 0x10de) {
        physical = p;
        printf("GPU %s driver=%u\n", pr.deviceName, pr.driverVersion);
        break;
      }
    }
    require(physical, "NVIDIA device");
    VkPhysicalDeviceIDProperties ids{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &ids;
    i<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(
        physical, &props);
    printf("FG_DEVICE_ID luid_valid=%u luid=%s uuid=%s\n", ids.deviceLUIDValid,
           "see_physical_device_properties", hex(ids.deviceUUID).c_str());
    NVSDK_NGX_FeatureDiscoveryInfo discovery{};
    discovery.SDKVersion = NVSDK_NGX_Version_API;
    discovery.FeatureID = NVSDK_NGX_Feature_FrameGeneration;
    discovery.Identifier.IdentifierType =
        NVSDK_NGX_Application_Identifier_Type_Project_Id;
    discovery.Identifier.v.ProjectDesc = {
        "42359704-c9f3-4806-9fb5-d469000fdb8a", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "bbport-M2"};
    auto data = wide(run);
    discovery.ApplicationDataPath = data.c_str();
    NVSDK_NGX_FeatureRequirement req{};
    auto qr = sym<decltype(&NVSDK_NGX_VULKAN_GetFeatureRequirements)>(
        core, "NVSDK_NGX_VULKAN_GetFeatureRequirements")(instance, physical,
                                                         &discovery, &req);
    printf("FG_CORE_REQUIREMENTS result=0x%08x support_bits=%u\n", unsigned(qr),
           unsigned(req.FeatureSupported));
    uint32_t required_n = 0;
    VkExtensionProperties *required = nullptr;
    auto er =
        sym<decltype(&NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements)>(
            core, "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements")(
            instance, physical, &discovery, &required_n, &required);
    printf("FG_CORE_DEVICE_EXTENSIONS result=0x%08x count=%u\n", unsigned(er),
           required_n);
    api(i<PFN_vkEnumerateDeviceExtensionProperties>(
            "vkEnumerateDeviceExtensionProperties")(physical, nullptr, &n,
                                                    nullptr),
        "extension count");
    std::vector<VkExtensionProperties> available(n);
    api(i<PFN_vkEnumerateDeviceExtensionProperties>(
            "vkEnumerateDeviceExtensionProperties")(physical, nullptr, &n,
                                                    available.data()),
        "extensions");
    std::vector<std::string> names;
    auto add = [&](const char *name) {
      require(std::any_of(
                  available.begin(), available.end(),
                  [&](auto &e) { return strcmp(e.extensionName, name) == 0; }),
              name);
      if (std::find(names.begin(), names.end(), name) == names.end())
        names.emplace_back(name);
    };
    for (const char *e : {"VK_NVX_binary_import", "VK_NVX_image_view_handle",
                          VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
                          VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                          VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                          VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                          VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME})
      add(e);
    for (const char *e : {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                          VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME}) {
      const bool present =
          std::any_of(available.begin(), available.end(),
                      [&](auto &x) { return strcmp(x.extensionName, e) == 0; });
      printf("FG_WIN32_INTEROP name=%s supported=%u\n", e, present);
      if (present)
        add(e);
    }
    if (er == NVSDK_NGX_Result_Success)
      for (unsigned j = 0; j < required_n; ++j)
        add(required[j].extensionName);
    std::vector<const char *> enabled;
    for (auto &name : names) {
      enabled.push_back(name.c_str());
      printf("FG_EXTENSION %s\n", name.c_str());
    }
    i<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        "vkGetPhysicalDeviceQueueFamilyProperties")(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> queues(n);
    i<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        "vkGetPhysicalDeviceQueueFamilyProperties")(physical, &n,
                                                    queues.data());
    bool found = false;
    for (unsigned j = 0; j < n; ++j)
      if (queues[j].queueCount &&
          (queues[j].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        family = j;
        found = true;
        break;
      }
    require(found, "compute family");
    VkPhysicalDeviceVulkan13Features f13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f.pNext = &f12;
    i<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(
        physical, &f);
    float priority = .5f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.pNext = &f;
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &q;
    dc.enabledExtensionCount = uint32_t(enabled.size());
    dc.ppEnabledExtensionNames = enabled.data();
    api(i<PFN_vkCreateDevice>("vkCreateDevice")(physical, &dc, nullptr,
                                                &device),
        "device");
    gdpa = i<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    d<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, family, 0, &queue);
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.queueFamilyIndex = family;
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    api(d<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device, &pc, nullptr,
                                                          &pool),
        "pool");
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    api(d<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device, &ca,
                                                                    &command),
        "command");
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    api(d<PFN_vkCreateFence>("vkCreateFence")(device, &fc, nullptr, &fence),
        "fence");
    core_attempted = true;
    auto ir = sym<decltype(&NVSDK_NGX_VULKAN_Init_Ext)>(
        core, "NVSDK_NGX_VULKAN_Init_Ext")(0x24480451ull, data.c_str(),
                                           instance, physical, device,
                                           NVSDK_NGX_Version_API, nullptr);
    printf("FG_CORE_INIT result=0x%08x\n", unsigned(ir));
    if (ir != NVSDK_NGX_Result_Success)
      return;
    auto cr = sym<decltype(&NVSDK_NGX_VULKAN_GetCapabilityParameters)>(
        core, "NVSDK_NGX_VULKAN_GetCapabilityParameters")(&params);
    printf("FG_CORE_CAPS result=0x%08x params=%p\n", unsigned(cr), params);
    require(cr == NVSDK_NGX_Result_Success && params, "core caps");
    auto vt = *reinterpret_cast<void ***>(params);
    using Set = void(__thiscall *)(void *, const char *, unsigned);
    using Get = int(__thiscall *)(void *, const char *, unsigned *);
    auto set = reinterpret_cast<Set>(vt[3]);
    auto get = reinterpret_cast<Get>(vt[11]);
    set(params, "bbport.FGProbe", 0x31579u);
    alignas(16) unsigned char canary[64];
    memset(canary, 0xa5, sizeof canary);
    auto gr = get(params, "bbport.FGProbe",
                  reinterpret_cast<unsigned *>(canary + 16));
    unsigned value = 0;
    memcpy(&value, canary + 16, 4);
    for (unsigned j = 0; j < 64; ++j)
      require((j >= 16 && j < 20) || canary[j] == 0xa5, "uint ABI canary");
    require(gr == 1 && value == 0x31579u, "uint ABI roundtrip");
    for (const char *key :
         {"FrameGeneration.Available", "FrameGeneration.FeatureInitResult",
          "FrameGeneration.NeedsUpdatedDriver"}) {
      unsigned v = 0;
      int r = get(params, key, &v);
      printf("FG_CAP key=%s result=0x%08x value=0x%08x\n", key, unsigned(r), v);
    }
    set(params, "Width", feature_layout.output_width);
    set(params, "Height", feature_layout.output_height);
    set(params, "CreationNodeMask", 1);
    set(params, "VisibilityNodeMask", 1);
    set(params, "DLSSG.BackbufferFormat", VK_FORMAT_R8G8B8A8_UNORM);
    set(params, "DLSSG.MultiFrameCount", generated_count);
    if (snippet) {
      // The snippet reads the documented logging fields from its Init parameter
      // block. Keep vendor error messages; do not replace failed capabilities
      // or return values.
      reinterpret_cast<void(__thiscall *)(void *, const char *, int)>(vt[4])(
          params, "Minimum.Logging.Level", 2);
      if(getenv("FG_POC_STREAM")) {
        reinterpret_cast<void(__thiscall*)(void*,const char*,int)>(vt[4])(params,"Minimum.Logging.Level",1);
        reinterpret_cast<void(__thiscall*)(void*,const char*,int)>(vt[4])(params,"Disable.Other.Logging.Sinks",1);
      }
      reinterpret_cast<void(__thiscall *)(void *, const char *,
                                          unsigned long long)>(vt[0])(
          params, "Log.Callback",
          reinterpret_cast<unsigned long long>(&model_log));
      if (const char *cuda_path = getenv("FG_POC_NVCUDA")) {
        SetLastError(0);
        cuda = LoadLibraryExW(wide(win(cuda_path)).c_str(), nullptr,
                              LOAD_WITH_ALTERED_SEARCH_PATH);
        printf("FG_CUDA_PRELOAD handle=%p winerror=%lu cuInit_export=%p\n",
               cuda, (unsigned long)GetLastError(),
               cuda ? reinterpret_cast<void *>(GetProcAddress(cuda, "cuInit"))
                    : nullptr);
        require(cuda && GetProcAddress(cuda, "cuInit"),
                "Proton CUDA interop module");
      }
      bridge = LoadLibraryW(wide(win(adapter)).c_str());
      require(bridge, "FG caller adapter");
      const char *model_path = getenv("FG_POC_MODEL");
      require(model_path, "explicit FG model path");
      require(sym<int (*)(const wchar_t *)>(bridge, "fg_load")(
                  wide(win(model_path)).c_str()) == 1,
              "FG model load");
      if(generated_count>1)MfgUnlock::Apply(GetModuleHandleW(L"nvngx_dlssg.dll"));
      NVSDK_NGX_FeatureRequirement r{};
      int qr = sym<int (*)(void *, void *, const void *, void *)>(
          bridge, "fg_requirements")(instance, physical, &discovery, &r);
      printf("FG_SNIPPET_REQUIREMENTS result=0x%08x support_bits=%u\n",
             unsigned(qr), unsigned(r.FeatureSupported));
      model_attempted = true;
      int ir = sym<int (*)(const wchar_t *, void *, void *, void *, int,
                           const void *)>(bridge, "fg_init")(
          data.c_str(), instance, physical, device, NVSDK_NGX_Version_API,
          params);
      printf("FG_SNIPPET_INIT result=0x%08x\n", unsigned(ir));
      if (ir != 1)
        return;
      int pr = sym<int (*)(void *)>(bridge, "fg_populate")(params);
      printf("FG_SNIPPET_POPULATE result=0x%08x\n", unsigned(pr));
      require(pr == 1, "FG model populate");
      if(generated_count>1){unsigned maximum=0;require(get(params,"DLSSG.MultiFrameCountMax",&maximum)==1&&maximum>=generated_count,"MFG actual model maximum");printf("DLSSG_MFG_CAP maximum=%u requested=%u\n",maximum,generated_count); }
    }
    set(params,"DLSSG.MultiFrameCount",generated_count);
    // Create and Evaluate share one immutable negotiated layout. A new layout
    // is a fully retired/new feature generation, not an unchecked size update.
    for (auto k : {"DLSSG.InternalWidth", "DLSSG.BackbufferSubrectWidth",
                   "DLSSG.MVecsSubrectWidth", "DLSSG.DepthSubrectWidth"})
      set(params, k, feature_layout.output_width);
    for (auto k : {"DLSSG.InternalHeight", "DLSSG.BackbufferSubrectHeight",
                   "DLSSG.MVecsSubrectHeight", "DLSSG.DepthSubrectHeight"})
      set(params, k, feature_layout.output_height);
    set(params, "DLSSG.DynamicResolution", 0);
    if (getenv("FG_POC_INPUT_MANIFEST")||getenv("FG_POC_STREAM")) {
      set(params,"DLSSG.InternalWidth",feature_layout.render_width); set(params,"DLSSG.InternalHeight",feature_layout.render_height);
      set(params,"DLSSG.MVecsSubrectWidth",feature_layout.render_width); set(params,"DLSSG.MVecsSubrectHeight",feature_layout.render_height);
      set(params,"DLSSG.DepthSubrectWidth",feature_layout.render_width); set(params,"DLSSG.DepthSubrectHeight",feature_layout.render_height);
    }
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    api(d<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command, &bi),
        "begin create");
    create_result =
        snippet ? unsigned(sym<int (*)(void *, int, const void *, void **)>(
                      bridge, "fg_create")(command, 11, params,
                                           reinterpret_cast<void **>(&feature)))
                : unsigned(sym<decltype(&NVSDK_NGX_VULKAN_CreateFeature)>(
                      core, "NVSDK_NGX_VULKAN_CreateFeature")(
                      command, NVSDK_NGX_Feature_FrameGeneration, params,
                      &feature));
    printf("FG_CORE_CREATE result=0x%08x feature=%p recorded_only=1\n",
           create_result, feature);
    api(d<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command), "end create");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command;
    inflight = true;
    api(d<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &si, fence),
        "submit create");
    api(d<PFN_vkWaitForFences>("vkWaitForFences")(device, 1, &fence, VK_TRUE,
                                                  10000000000ull),
        "create actual GPU fence");
    inflight = false;
    printf(
        "FG_CREATE_GPU_COMPLETE result=0x%08x generated_output=NOT_EVALUATED\n",
        create_result);
  }
  void evaluate(const char *run);
  void stream(const char *run);
  void retire() {
    require(!inflight && !shared_active, "GPU/shared peer retirement boundary");
    if (device)
      api(d<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device),
          "device idle before release");
    if (feature) {
      int r = snippet ? sym<int (*)(void *)>(bridge, "fg_release")(feature)
                      : int(sym<decltype(&NVSDK_NGX_VULKAN_ReleaseFeature)>(
                            core, "NVSDK_NGX_VULKAN_ReleaseFeature")(feature));
      printf("FG_RELEASE result=0x%08x\n", unsigned(r));
      require(r == NVSDK_NGX_Result_Success, "release feature");
      feature = nullptr;
    }
    if (model_attempted) {
      int r = sym<int (*)(void *)>(bridge, "fg_shutdown")(device);
      printf("FG_SNIPPET_SHUTDOWN result=0x%08x\n", unsigned(r));
      require(r == 1, "FG model shutdown");
    }
    if (device) api(d<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device),
                    "device idle after model shutdown before private images");
    if (bridge) {
      require(sym<int (*)()>(bridge, "fg_unload")() == 1, "model unload");
      require(FreeLibrary(bridge), "bridge unload");
      bridge = nullptr;
    }
    fixture.reset();
    input_descriptors.reset();
    if (params) {
      require(sym<decltype(&NVSDK_NGX_VULKAN_DestroyParameters)>(
                  core, "NVSDK_NGX_VULKAN_DestroyParameters")(params) ==
                  NVSDK_NGX_Result_Success,
              "destroy params");
      params = nullptr;
    }
    if (core_attempted) {
      using Shutdown = int(__cdecl *)();
      int r = sym<Shutdown>(core, "NVSDK_NGX_VULKAN_Shutdown")();
      printf("FG_CORE_SHUTDOWN result=0x%08x\n", unsigned(r));
      require(r == 1, "core shutdown");
    }
    if (device) {
      api(d<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device),
          "device idle after shutdown");
      if (fence)
        d<PFN_vkDestroyFence>("vkDestroyFence")(device, fence, nullptr);
      if (pool)
        d<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device, pool,
                                                            nullptr);
      d<PFN_vkDestroyDevice>("vkDestroyDevice")(device, nullptr);
      device = nullptr;
    }
    if (instance) {
      i<PFN_vkDestroyInstance>("vkDestroyInstance")(instance, nullptr);
      instance = nullptr;
    }
    for (HMODULE m : {core, cuda, nvapi, dxgi, loader})
      if (m)
        require(FreeLibrary(m), "FreeLibrary");
    puts("FG_POC_RETIRED actual_GPU_complete=1 feature_released=1 "
         "core_shutdown=1 device_destroyed=1 native_readers=0");
  }
};
#include "dlssg_evaluate_fixture.h"
#include "dlssg_stream.h"
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc != 3 && argc != 4)
    return 2;
  Probe p;
  bool retiring = false;
  try {
    p.init(argv[1], argv[2], argc == 4 ? argv[3] : nullptr);
    if (getenv("FG_POC_STREAM")) p.stream(argv[1]);
    else if (getenv("FG_POC_EVALUATE") && p.feature && p.create_result == 1)
      p.evaluate(argv[1]);
    retiring = true;
    p.retire();
    if(getenv("FG_POC_STREAM")){require(!p.stream_terminal.is_null(),"stream fully consumed terminal");p.stream_terminal.update(nlohmann::json{{"actual_GPU_complete",true},{"feature_released",true},{"device_destroyed",true},{"native_gpu_readers",0}});atomic_text(std::string(argv[1])+"/stream-retired.json",p.stream_terminal.dump()+"\n");}
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "FG_POC_UNAVAILABLE reason=%s\n", e.what());
    if (!retiring && !p.inflight && !p.shared_active) {
      try {
        p.retire();
        return 0;
      } catch (const std::exception &r) {
        fprintf(stderr, "FG_POC_RETIRE_ERROR %s\n", r.what());
      }
    }
    if (p.shared_active && p.fixture) p.fixture->safe=false;
    fprintf(stderr,
            "FG_POC_HOLD actual_GPU_completion=UNKNOWN forced_retirement=0\n");
    return 3;
  }
}
