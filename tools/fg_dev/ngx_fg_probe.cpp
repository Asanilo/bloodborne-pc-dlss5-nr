// SPDX-License-Identifier: MIT
// Capability discovery only: no VkDevice, feature creation, dispatch, or presentation.
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <cstdio>
#include <filesystem>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: ngx-fg-probe FEATURE_DIRECTORY PRIVATE_DATA_DIRECTORY\n");
        return 2;
    }
    const auto directory = std::filesystem::absolute(argv[1]).wstring();
    const auto data = std::filesystem::absolute(argv[2]).wstring();
    std::filesystem::create_directories(argv[2]);
    const wchar_t* path = directory.c_str();
    NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo = {&path, 1};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance{};
    if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS) return 3;
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    bool found = false;
    for (auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.vendorID != 0x10de) continue;
        found = true;
        std::printf("GPU name=%s driver=%u\n", properties.deviceName, properties.driverVersion);
        for (auto feature : {NVSDK_NGX_Feature_SuperSampling, NVSDK_NGX_Feature_FrameGeneration}) {
            NVSDK_NGX_FeatureDiscoveryInfo discovery{};
            discovery.SDKVersion = NVSDK_NGX_Version_API;
            discovery.FeatureID = feature;
            discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
            discovery.Identifier.v.ProjectDesc = {"42359704-c9f3-4806-9fb5-d469000fdb8a",
                                                   NVSDK_NGX_ENGINE_TYPE_CUSTOM, "bbport-linux"};
            discovery.ApplicationDataPath = data.c_str();
            discovery.FeatureInfo = &common;
            NVSDK_NGX_FeatureRequirement requirement{};
            const auto result = NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, device,
                                                                        &discovery, &requirement);
            std::printf("NGX_DISCOVERY feature=%s result=0x%08x support_bits=0x%x minimum_arch=0x%x minimum_OS=%s scope=requirements_only_no_feature_or_GPU_work\n",
                feature == NVSDK_NGX_Feature_FrameGeneration ? "FrameGeneration" : "SuperSampling",
                unsigned(result), unsigned(requirement.FeatureSupported),
                requirement.MinHWArchitecture, requirement.MinOSVersion);
        }
    }
    vkDestroyInstance(instance, nullptr);
    std::printf("NGX_PROBE_RETIRED instance_destroyed=1 devices_created=0 GPU_submissions=0\n");
    return found ? 0 : 4;
}
