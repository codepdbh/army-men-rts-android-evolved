#include "vulkan_renderer.h"
#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" failed: Vulkan "+std::to_string(result));
}
struct TestResources {
    VkDevice device;
    VkImage image=VK_NULL_HANDLE;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory imageMemory=VK_NULL_HANDLE,bufferMemory=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkCommandPool pool;
    VkCommandBuffer command=VK_NULL_HANDLE;
    void* mapped=nullptr;
    ~TestResources() {
        vkDeviceWaitIdle(device);
        if(mapped)vkUnmapMemory(device,bufferMemory);
        if(fence)vkDestroyFence(device,fence,nullptr);
        if(command)vkFreeCommandBuffers(device,pool,1,&command);
        if(image)vkDestroyImage(device,image,nullptr);
        if(buffer)vkDestroyBuffer(device,buffer,nullptr);
        if(imageMemory)vkFreeMemory(device,imageMemory,nullptr);
        if(bufferMemory)vkFreeMemory(device,bufferMemory,nullptr);
    }
};
}

VulkanRenderer::~VulkanRenderer() {
    if(device_){vkDeviceWaitIdle(device_);if(pool_)vkDestroyCommandPool(device_,pool_,nullptr);vkDestroyDevice(device_,nullptr);}
    if(instance_)vkDestroyInstance(instance_,nullptr);
}

void VulkanRenderer::initialize(bool createDevice) {
    if(!instance_){
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName="Army Men RTS Android Evolved";application.applicationVersion=VK_MAKE_VERSION(0,2,0);
        application.pEngineName="Android Evolved";application.engineVersion=1;application.apiVersion=VK_API_VERSION_1_1;
        const char* extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo=&application;instanceInfo.enabledExtensionCount=2;instanceInfo.ppEnabledExtensionNames=extensions;
        check(vkCreateInstance(&instanceInfo,nullptr,&instance_),"Vulkan 1.1 instance creation");
        uint32_t count=0;check(vkEnumeratePhysicalDevices(instance_,&count,nullptr),"Physical GPU count");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance_,&count,devices.data()),"Physical GPU enumeration");
        for(VkPhysicalDevice candidate:devices){
            VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(candidate,&properties);
            if(properties.apiVersion<VK_API_VERSION_1_1)continue;
            uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            for(uint32_t i=0;i<families;++i)if(queues[i].queueCount&&(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){
                physical_=candidate;family_=i;info_.name=properties.deviceName;info_.apiVersion=properties.apiVersion;
                info_.driverVersion=properties.driverVersion;info_.vendorId=properties.vendorID;info_.deviceId=properties.deviceID;
                info_.maxTextureSize=properties.limits.maxImageDimension2D;break;
            }
            if(physical_)break;
        }
        if(!physical_)throw std::runtime_error("No graphics GPU with Vulkan 1.1 support");
        vkGetPhysicalDeviceMemoryProperties(physical_,&memory_);
        for(uint32_t i=0;i<memory_.memoryHeapCount;++i)if(memory_.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            info_.deviceMemory+=memory_.memoryHeaps[i].size;
    }
    if(!createDevice||device_)return;
    float priority=1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex=family_;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    const char* extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
    deviceInfo.enabledExtensionCount=1;deviceInfo.ppEnabledExtensionNames=extensions;
    check(vkCreateDevice(physical_,&deviceInfo,nullptr,&device_),"Vulkan device creation");
    vkGetDeviceQueue(device_,family_,0,&queue_);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family_;
    poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device_,&poolInfo,nullptr,&pool_),"Vulkan command pool creation");
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","Vulkan 1.1 backend: %s (GPU API %u.%u)",info_.name.c_str(),
        VK_API_VERSION_MAJOR(info_.apiVersion),VK_API_VERSION_MINOR(info_.apiVersion));
}

uint32_t VulkanRenderer::memoryType(uint32_t bits,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred)const{
    for(unsigned pass=0;pass<2;++pass)for(uint32_t i=0;i<memory_.memoryTypeCount;++i){
        VkMemoryPropertyFlags flags=required|(pass?0:preferred);
        if((bits&(1u<<i))&&(memory_.memoryTypes[i].propertyFlags&flags)==flags)return i;
    }
    throw std::runtime_error("Required Vulkan memory type unavailable");
}
bool VulkanRenderer::supportsOptimalFormat(VkFormat format,VkFormatFeatureFlags flags)const{
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(physical_,format,&properties);
    return (properties.optimalTilingFeatures&flags)==flags;
}

void VulkanRenderer::testClearReadback() {
    initialize();TestResources resources{device_,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool_};
    constexpr uint32_t width=64,height=64,bytes=width*height*4;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};imageInfo.imageType=VK_IMAGE_TYPE_2D;
    imageInfo.format=VK_FORMAT_R8G8B8A8_UNORM;imageInfo.extent={width,height,1};imageInfo.mipLevels=1;imageInfo.arrayLayers=1;
    imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;imageInfo.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_,&imageInfo,nullptr,&resources.image),"Offscreen image creation");
    VkMemoryRequirements imageRequirements{};vkGetImageMemoryRequirements(device_,resources.image,&imageRequirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=imageRequirements.size;
    allocation.memoryTypeIndex=memoryType(imageRequirements.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&resources.imageMemory),"Offscreen image allocation");
    check(vkBindImageMemory(device_,resources.image,resources.imageMemory,0),"Offscreen image binding");
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=bytes;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device_,&bufferInfo,nullptr,&resources.buffer),"Readback buffer creation");
    VkMemoryRequirements bufferRequirements{};vkGetBufferMemoryRequirements(device_,resources.buffer,&bufferRequirements);
    allocation.allocationSize=bufferRequirements.size;
    allocation.memoryTypeIndex=memoryType(bufferRequirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&resources.bufferMemory),"Readback allocation");
    check(vkBindBufferMemory(device_,resources.buffer,resources.bufferMemory,0),"Readback buffer binding");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commandInfo.commandPool=pool_;
    commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&commandInfo,&resources.command),"Offscreen command allocation");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(resources.command,&begin),"Offscreen command begin");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=resources.image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkClearColorValue green{};green.float32[1]=green.float32[3]=1.0f;
    vkCmdClearColorImage(resources.command,resources.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&green,1,&barrier.subresourceRange);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
    vkCmdCopyImageToBuffer(resources.command,resources.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,resources.buffer,1,&copy);
    VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};hostBarrier.buffer=resources.buffer;hostBarrier.size=VK_WHOLE_SIZE;
    hostBarrier.srcQueueFamilyIndex=hostBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;hostBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&hostBarrier,0,nullptr);
    check(vkEndCommandBuffer(resources.command),"Offscreen command end");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fenceInfo,nullptr,&resources.fence),"Offscreen fence creation");
    VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};submission.commandBufferCount=1;submission.pCommandBuffers=&resources.command;
    check(vkQueueSubmit(queue_,1,&submission,resources.fence),"Offscreen submit");
    check(vkWaitForFences(device_,1,&resources.fence,VK_TRUE,2000000000ULL),"Offscreen GPU wait");
    check(vkMapMemory(device_,resources.bufferMemory,0,VK_WHOLE_SIZE,0,&resources.mapped),"Readback mapping");
    VkMappedMemoryRange invalidate{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};invalidate.memory=resources.bufferMemory;invalidate.size=VK_WHOLE_SIZE;
    check(vkInvalidateMappedMemoryRanges(device_,1,&invalidate),"Readback visibility");
    const uint8_t* pixels=static_cast<const uint8_t*>(resources.mapped);
    for(uint32_t i=0;i<bytes;i+=4)if(pixels[i]!=0||pixels[i+1]!=255||pixels[i+2]!=0||pixels[i+3]!=255)
        throw std::runtime_error("Vulkan 1.1 clear/readback pixel mismatch");
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","Vulkan 1.1 offscreen clear/readback passed: %ux%u RGBA",width,height);
}
