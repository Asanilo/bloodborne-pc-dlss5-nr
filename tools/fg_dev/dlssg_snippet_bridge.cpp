// SPDX-License-Identifier: GPL-3.0-or-later
// Caller adapter only. No capability patching, MFG changes, resource/sync
// bypass.
#define NOMINMAX
#include <cstdio>
#include <windows.h>
static HMODULE model{};
extern "C" {
__declspec(dllexport) int fg_load(const wchar_t *path) {
  if (!model) {
    SetLastError(0);
    model = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    printf("FG_MODEL_LOAD handle=%p winerror=%lu\n", model,
           (unsigned long)GetLastError());
  }
  return model ? 1 : 0;
}
__declspec(dllexport) int fg_requirements(void *instance, void *physical,
                                          const void *discovery,
                                          void *requirement) {
  using Fn = int(__cdecl *)(void *, void *, const void *, void *);
  auto f = reinterpret_cast<Fn>(
      GetProcAddress(model, "NVSDK_NGX_VULKAN_GetFeatureRequirements"));
  if (!f)
    return -1;
  volatile int r = f(instance, physical, discovery, requirement);
  return r;
}
__declspec(dllexport) int fg_init(const wchar_t *path, void *instance,
                                  void *physical, void *device, int version,
                                  const void *params) {
  // FG's SDK snippet order: version then params. NR's private adapter differs.
  using Fn = int(__cdecl *)(unsigned long long, const wchar_t *, void *, void *,
                            void *, int, const void *);
  auto f =
      reinterpret_cast<Fn>(GetProcAddress(model, "NVSDK_NGX_VULKAN_Init_Ext"));
  if (!f)
    return -1;
  volatile int r =
      f(0x24480451ull, path, instance, physical, device, version, params);
  return r;
}
__declspec(dllexport) int fg_populate(void *params) {
  using Fn = int(__cdecl *)(void *);
  auto f = reinterpret_cast<Fn>(
      GetProcAddress(model, "NVSDK_NGX_VULKAN_PopulateParameters_Impl"));
  if (!f)
    return -1;
  volatile int r = f(params);
  return r;
}
__declspec(dllexport) int fg_create(void *cmd, int feature, const void *params,
                                    void **out) {
  using Fn = int(__cdecl *)(void *, int, const void *, void **);
  auto f = reinterpret_cast<Fn>(
      GetProcAddress(model, "NVSDK_NGX_VULKAN_CreateFeature"));
  if (!f)
    return -1;
  volatile int r = f(cmd, feature, params, out);
  return r;
}
__declspec(dllexport) int fg_evaluate(void *cmd, const void *feature,
                                      const void *params) {
  using Fn = int(__cdecl *)(void *, const void *, const void *, void *);
  auto f = reinterpret_cast<Fn>(
      GetProcAddress(model, "NVSDK_NGX_VULKAN_EvaluateFeature"));
  if (!f)
    return -1;
  volatile int r = f(cmd, feature, params, nullptr);
  return r;
}
__declspec(dllexport) int fg_release(void *feature) {
  using Fn = int(__cdecl *)(void *);
  auto f = reinterpret_cast<Fn>(
      GetProcAddress(model, "NVSDK_NGX_VULKAN_ReleaseFeature"));
  if (!f)
    return -1;
  volatile int r = f(feature);
  return r;
}
__declspec(dllexport) int fg_shutdown(void *device) {
  using Fn = int(__cdecl *)(void *);
  auto f =
      reinterpret_cast<Fn>(GetProcAddress(model, "NVSDK_NGX_VULKAN_Shutdown1"));
  if (!f)
    return -1;
  volatile int r = f(device);
  return r;
}
__declspec(dllexport) int fg_unload() {
  if (!model)
    return 1;
  int r = FreeLibrary(model);
  if (r)
    model = nullptr;
  return r;
}
}
