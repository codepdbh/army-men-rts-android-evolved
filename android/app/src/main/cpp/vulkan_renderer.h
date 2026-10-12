#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

struct VulkanGpuInfo {
    std::string name;
    uint32_t apiVersion=0, driverVersion=0, vendorId=0, deviceId=0, maxTextureSize=0;
    uint64_t deviceMemory=0;
};

// Vulkan 1.1 is the explicit instance/device baseline. No 1.2/1.3 entry points.
class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    void initialize(bool createDevice=true);
    const VulkanGpuInfo& gpu() const { return info_; }
    void testClearReadback();
    bool supportsOptimalFormat(VkFormat format,VkFormatFeatureFlags flags) const;
private:
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred=0) const;
    VkInstance instance_=VK_NULL_HANDLE;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkDevice device_=VK_NULL_HANDLE;
    VkQueue queue_=VK_NULL_HANDLE;
    VkCommandPool pool_=VK_NULL_HANDLE;
    uint32_t family_=0;
    VkPhysicalDeviceMemoryProperties memory_{};
    VulkanGpuInfo info_;
};
