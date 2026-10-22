// Isolated fixture-only lifecycle adapter linked beside the unchanged production
// forwarder, so the vendor's caller-module gate remains satisfied at shutdown.
#define NOMINMAX
#include <windows.h>
extern "C" __declspec(dllexport) int nr_poc_vk_shutdown(const wchar_t* modelPath, void* device)
{
    HMODULE model = GetModuleHandleW(modelPath); // Borrow; never unload another owner's reference.
    if (!model)
        return -1;
    using Shutdown1 = int(__cdecl*)(void*);
    auto fn = reinterpret_cast<Shutdown1>(GetProcAddress(model, "NVSDK_NGX_VULKAN_Shutdown1"));
    if (!fn)
        return -2;
    volatile int result = fn(device); // No tail call across the caller-module check.
    return result;
}
