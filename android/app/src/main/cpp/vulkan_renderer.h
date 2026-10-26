#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <memory>
#ifdef __ANDROID__
#include <android/native_window.h>
#else
struct ANativeWindow;
#endif
#include <vector>
#include <array>
#include <map>
struct VulkanPipeline;
struct VulkanDrawBatch;
struct VulkanSurface {
    VkDevice device=VK_NULL_HANDLE;
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    uint32_t width=0,height=0;
    bool depth=false;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageView view=VK_NULL_HANDLE;
    ~VulkanSurface();
};

struct VulkanGpuInfo {
    std::string name;
    uint32_t apiVersion=0, driverVersion=0, vendorId=0, deviceId=0, maxTextureSize=0;
    uint64_t deviceMemory=0;
};
struct VulkanVertex {float position[4],color[4],uv0[2],uv1[2];};
struct VulkanDrawState {
    uint32_t primitive=4,cull=1,sourceBlend=2,destinationBlend=1,zCompare=4;
    bool blend=false,zTest=false,zWrite=false;
    uint32_t stages[8]={4,2,0,2|(2<<8),1,2,1,1};
    uint32_t alpha[4]{};
    float factor[4]={1,1,1,1};
    float viewport[6]{};
    // D3DTADDRESS per stage (u0, v0, u1, v1): 1 wrap, 2 mirror, 3 clamp, 4 border.
    uint32_t address[4]={1,1,1,1};
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
    void testTriangleReadback();
    std::vector<uint8_t> readSurface(VulkanSurface& surface);
    std::shared_ptr<VulkanSurface> createSurface(uint32_t width,uint32_t height,bool depth,uint32_t bits);
    void uploadSurface(VulkanSurface& surface,const void* rgba);
    void clearSurface(VulkanSurface& surface,uint32_t argb,float depth);
    void clearRegions(VulkanSurface& target,VulkanSurface* depth,uint32_t flags,uint32_t argb,float value,const std::vector<VkRect2D>& regions);
    void setWindow(ANativeWindow* window);
    void present(VulkanSurface& surface);
    void flushDraws();
    void draw(VulkanSurface& target,VulkanSurface* depth,VulkanSurface* texture0,VulkanSurface* texture1,
              const std::vector<VulkanVertex>& vertices,const std::vector<uint16_t>& indices,const VulkanDrawState& state);
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
    VkSurfaceKHR display_=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_=VK_NULL_HANDLE;
    VkExtent2D displayExtent_{};
    std::vector<VkImage> displayImages_;
    VkPipelineCache pipelineCache_=VK_NULL_HANDLE;
    std::shared_ptr<VulkanSurface> whiteTexture_;
    std::map<uint32_t,VkSampler> samplers_;
    VkSampler sampler(uint32_t u,uint32_t v);
    std::map<std::array<uint32_t,9>,std::shared_ptr<VulkanPipeline>> pipelines_;
    std::vector<std::shared_ptr<VulkanDrawBatch>> pendingDraws_;
};
