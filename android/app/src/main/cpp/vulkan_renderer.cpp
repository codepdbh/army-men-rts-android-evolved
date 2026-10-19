#include "vulkan_renderer.h"
#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#include <cstdarg>
constexpr int ANDROID_LOG_INFO=4;
static void __android_log_print(int,const char*,const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::putchar('\n');va_end(args);}
#endif
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "shaders/fixed_spirv.h"

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
        // Every successful submission waits for this fence before returning.
        // Keep the global wait only for exceptional cleanup of unfinished work.
        if(!fence||vkGetFenceStatus(device,fence)!=VK_SUCCESS)vkDeviceWaitIdle(device);
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
    if(device_)vkDeviceWaitIdle(device_);
    pendingDraws_.clear();
    pipelines_.clear();whiteTexture_.reset();
    if(pipelineCache_)vkDestroyPipelineCache(device_,pipelineCache_,nullptr);
    if(device_){vkDeviceWaitIdle(device_);if(swapchain_)vkDestroySwapchainKHR(device_,swapchain_,nullptr);if(pool_)vkDestroyCommandPool(device_,pool_,nullptr);vkDestroyDevice(device_,nullptr);}
    if(display_)vkDestroySurfaceKHR(instance_,display_,nullptr);
    if(instance_)vkDestroyInstance(instance_,nullptr);
}

VulkanSurface::~VulkanSurface(){
    if(view)vkDestroyImageView(device,view,nullptr);
    if(image)vkDestroyImage(device,image,nullptr);
    if(memory)vkFreeMemory(device,memory,nullptr);
}
std::shared_ptr<VulkanSurface> VulkanRenderer::createSurface(uint32_t width,uint32_t height,bool depth,uint32_t bits){
    auto surface=std::make_shared<VulkanSurface>();surface->device=device_;surface->width=width;surface->height=height;surface->depth=depth;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
    info.format=depth?(bits==16?VK_FORMAT_D16_UNORM:VK_FORMAT_D32_SFLOAT):VK_FORMAT_R8G8B8A8_UNORM;
    surface->format=info.format;
    info.extent={width,height,1};info.mipLevels=1;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
    info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.usage|=depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
    check(vkCreateImage(device_,&info,nullptr,&surface->image),"Surface image creation");
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device_,surface->image,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&surface->memory),"Surface memory allocation");
    check(vkBindImageMemory(device_,surface->image,surface->memory,0),"Surface memory binding");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=surface->image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
    view.subresourceRange={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
    check(vkCreateImageView(device_,&view,nullptr,&surface->view),"Surface image view");return surface;
}

void VulkanRenderer::initialize(bool createDevice) {
    if(!instance_){
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName="Army Men RTS Android Evolved";application.applicationVersion=VK_MAKE_VERSION(0,2,0);
        application.pEngineName="Android Evolved";application.engineVersion=1;application.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo=&application;
#ifdef __ANDROID__
        const char* extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        instanceInfo.enabledExtensionCount=2;instanceInfo.ppEnabledExtensionNames=extensions;
#endif
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
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
#ifdef __ANDROID__
    const char* extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    deviceInfo.enabledExtensionCount=1;deviceInfo.ppEnabledExtensionNames=extensions;
#endif
    check(vkCreateDevice(physical_,&deviceInfo,nullptr,&device_),"Vulkan device creation");
    vkGetDeviceQueue(device_,family_,0,&queue_);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family_;
    poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device_,&poolInfo,nullptr,&pool_),"Vulkan command pool creation");
    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    check(vkCreatePipelineCache(device_,&cache,nullptr,&pipelineCache_),"Vulkan pipeline cache");
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

void VulkanRenderer::uploadSurface(VulkanSurface& surface,const void* rgba){
    flushDraws();
    if(surface.depth)throw std::runtime_error("Depth surface CPU upload unsupported");
    TestResources resources{device_,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool_};
    uint64_t bytes=uint64_t(surface.width)*surface.height*4;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=bytes;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(vkCreateBuffer(device_,&buffer,nullptr,&resources.buffer),"Upload buffer creation");
    VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device_,resources.buffer,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&resources.bufferMemory),"Upload allocation");
    check(vkBindBufferMemory(device_,resources.buffer,resources.bufferMemory,0),"Upload binding");
    check(vkMapMemory(device_,resources.bufferMemory,0,VK_WHOLE_SIZE,0,&resources.mapped),"Upload mapping");
    std::memcpy(resources.mapped,rgba,bytes);
    VkMappedMemoryRange flush{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};flush.memory=resources.bufferMemory;flush.size=VK_WHOLE_SIZE;
    check(vkFlushMappedMemoryRanges(device_,1,&flush),"Upload visibility");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commands.commandPool=pool_;
    commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&commands,&resources.command),"Upload command allocation");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(resources.command,&begin),"Upload command begin");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=surface.image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.oldLayout=surface.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask=surface.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={surface.width,surface.height,1};
    vkCmdCopyBufferToImage(resources.command,resources.buffer,surface.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    check(vkEndCommandBuffer(resources.command),"Upload command end");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fence,nullptr,&resources.fence),"Upload fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&resources.command;
    check(vkQueueSubmit(queue_,1,&submit,resources.fence),"Upload submit");
    check(vkWaitForFences(device_,1,&resources.fence,VK_TRUE,2000000000ULL),"Upload completion");surface.layout=VK_IMAGE_LAYOUT_GENERAL;
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

void VulkanRenderer::setWindow(ANativeWindow* window){
#ifdef __ANDROID__
    if(!window)return;
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};info.window=window;
    check(vkCreateAndroidSurfaceKHR(instance_,&info,nullptr,&display_),"Android Vulkan surface");
    VkBool32 supported=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_,family_,display_,&supported),"Presentation support");
    if(!supported)throw std::runtime_error("GPU queue cannot present to Android");
    VkSurfaceCapabilitiesKHR caps{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_,display_,&caps),"Display capabilities");
    if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Display transfer unavailable");
    uint32_t count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,display_,&count,nullptr),"Display format count");
    std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,display_,&count,formats.data()),"Display formats");
    if(formats.empty())throw std::runtime_error("No Android surface formats");
    VkSurfaceFormatKHR format=formats[0];for(auto candidate:formats)if(candidate.format==VK_FORMAT_R8G8B8A8_UNORM||candidate.format==VK_FORMAT_B8G8R8A8_UNORM){format=candidate;break;}
    if(format.format==VK_FORMAT_UNDEFINED)format.format=VK_FORMAT_R8G8B8A8_UNORM;
    if(!supportsOptimalFormat(format.format,VK_FORMAT_FEATURE_BLIT_DST_BIT))throw std::runtime_error("Display blit format unsupported");
    displayExtent_=caps.currentExtent;
    if(displayExtent_.width==UINT32_MAX)displayExtent_={std::clamp(uint32_t(ANativeWindow_getWidth(window)),caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(uint32_t(ANativeWindow_getHeight(window)),caps.minImageExtent.height,caps.maxImageExtent.height)};
    VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};swap.surface=display_;swap.minImageCount=caps.minImageCount+1;
    if(caps.maxImageCount)swap.minImageCount=std::min(swap.minImageCount,caps.maxImageCount);
    swap.imageFormat=format.format;swap.imageColorSpace=format.colorSpace;swap.imageExtent=displayExtent_;swap.imageArrayLayers=1;
    swap.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;swap.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    // Android applies the chosen transform to presented buffers. This backend
    // already draws in the SurfaceView's landscape coordinates.
    if(!(caps.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))throw std::runtime_error("Display requires unsupported prerotation");
    swap.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","Landscape display %ux%u window=%dx%d currentTransform=%u supported=%u",displayExtent_.width,displayExtent_.height,ANativeWindow_getWidth(window),ANativeWindow_getHeight(window),uint32_t(caps.currentTransform),uint32_t(caps.supportedTransforms));
    swap.compositeAlpha=VkCompositeAlphaFlagBitsKHR(caps.supportedCompositeAlpha&(~caps.supportedCompositeAlpha+1));
    swap.presentMode=VK_PRESENT_MODE_FIFO_KHR;swap.clipped=VK_TRUE;
    check(vkCreateSwapchainKHR(device_,&swap,nullptr,&swapchain_),"Android swapchain");
    check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,nullptr),"Display image count");displayImages_.resize(count);
    check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,displayImages_.data()),"Display images");
#else
    if(window)throw std::runtime_error("Android presentation cannot be tested on the host");
#endif
}
void VulkanRenderer::present(VulkanSurface& surface){
    flushDraws();
    if(!swapchain_)return;
    if(surface.depth||surface.layout==VK_IMAGE_LAYOUT_UNDEFINED)throw std::runtime_error("Invalid display source");
    TestResources resources{device_,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool_};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fence,nullptr,&resources.fence),"Acquire fence");
    uint32_t imageIndex=0;VkResult acquired=vkAcquireNextImageKHR(device_,swapchain_,2000000000ULL,VK_NULL_HANDLE,resources.fence,&imageIndex);if(acquired!=VK_SUBOPTIMAL_KHR)check(acquired,"Display acquisition");
    check(vkWaitForFences(device_,1,&resources.fence,VK_TRUE,2000000000ULL),"Display acquisition wait");
    check(vkResetFences(device_,1,&resources.fence),"Display fence reset");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commands.commandPool=pool_;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&commands,&resources.command),"Present commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(resources.command,&begin),"Present begin");
    VkImageMemoryBarrier barriers[2]{};
    for(auto& barrier:barriers){barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
    barriers[0].image=surface.image;barriers[0].oldLayout=surface.layout;barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].image=displayImages_[imageIndex];barriers[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
    VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    blit.srcOffsets[1]={int32_t(surface.width),int32_t(surface.height),1};blit.dstOffsets[1]={int32_t(displayExtent_.width),int32_t(displayExtent_.height),1};
    const VkFilter displayFilter=supportsOptimalFormat(surface.format,VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    vkCmdBlitImage(resources.command,surface.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,displayImages_[imageIndex],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,displayFilter);
    barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[0].newLayout=surface.layout;barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barriers[0].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=0;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
    check(vkEndCommandBuffer(resources.command),"Present end");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&resources.command;
    check(vkQueueSubmit(queue_,1,&submit,resources.fence),"Present submission");
    check(vkWaitForFences(device_,1,&resources.fence,VK_TRUE,2000000000ULL),"Present render wait");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.swapchainCount=1;present.pSwapchains=&swapchain_;present.pImageIndices=&imageIndex;
    VkResult presented=vkQueuePresentKHR(queue_,&present);if(presented!=VK_SUBOPTIMAL_KHR)check(presented,"Android presentation");check(vkQueueWaitIdle(queue_),"Android present wait");
}

void VulkanRenderer::clearSurface(VulkanSurface& surface,uint32_t argb,float depth){
    flushDraws();
    TestResources resources{device_,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool_};
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commands.commandPool=pool_;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&commands,&resources.command),"Clear commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(resources.command,&begin),"Clear begin");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=surface.image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={VkImageAspectFlags(surface.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
    barrier.oldLayout=surface.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask=surface.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    if(surface.depth){VkClearDepthStencilValue value{depth,0};vkCmdClearDepthStencilImage(resources.command,surface.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&barrier.subresourceRange);}
    else {VkClearColorValue value{};value.float32[0]=((argb>>16)&255)/255.0f;value.float32[1]=((argb>>8)&255)/255.0f;value.float32[2]=(argb&255)/255.0f;value.float32[3]=(argb>>24)/255.0f;vkCmdClearColorImage(resources.command,surface.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&barrier.subresourceRange);}
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    check(vkEndCommandBuffer(resources.command),"Clear end");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fence,nullptr,&resources.fence),"Clear fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&resources.command;
    check(vkQueueSubmit(queue_,1,&submit,resources.fence),"Clear submit");check(vkWaitForFences(device_,1,&resources.fence,VK_TRUE,2000000000ULL),"Clear wait");surface.layout=VK_IMAGE_LAYOUT_GENERAL;
}

struct VulkanPipeline {
    VkDevice device;VkRenderPass pass=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;VkSampler sampler=VK_NULL_HANDLE;
    ~VulkanPipeline(){
        vkDeviceWaitIdle(device);
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(pass)vkDestroyRenderPass(device,pass,nullptr);
        if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(descriptorLayout)vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);
        if(sampler)vkDestroySampler(device,sampler,nullptr);
        if(vertex)vkDestroyShaderModule(device,vertex,nullptr);
        if(fragment)vkDestroyShaderModule(device,fragment,nullptr);
    }
};
namespace {
struct DrawFramebuffer {VkDevice device;VkFramebuffer handle=VK_NULL_HANDLE;~DrawFramebuffer(){if(handle)vkDestroyFramebuffer(device,handle,nullptr);}};
}
struct VulkanDrawBatch {
    TestResources upload;
    DrawFramebuffer frame;
    std::shared_ptr<VulkanPipeline> pipeline;
    VkDescriptorSet descriptor=VK_NULL_HANDLE;
    VulkanDrawBatch(VkDevice device,VkCommandPool pool):upload{device,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool},frame{device}{}
    ~VulkanDrawBatch(){
        if(upload.fence&&vkGetFenceStatus(upload.device,upload.fence)!=VK_SUCCESS)vkDeviceWaitIdle(upload.device);
        if(descriptor)vkFreeDescriptorSets(upload.device,pipeline->descriptors,1,&descriptor);
    }
};
void VulkanRenderer::flushDraws(){
    if(pendingDraws_.empty())return;
    VkFence fence=pendingDraws_.back()->upload.fence;
    check(vkWaitForFences(device_,1,&fence,VK_TRUE,2000000000ULL),"Draw batch wait");
    pendingDraws_.clear();
}
namespace {
VkBlendFactor blendFactor(uint32_t factor){
    static const VkBlendFactor table[]={VK_BLEND_FACTOR_ZERO,VK_BLEND_FACTOR_ZERO,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_SRC_COLOR,VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,VK_BLEND_FACTOR_SRC_ALPHA,VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,VK_BLEND_FACTOR_DST_ALPHA,VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,VK_BLEND_FACTOR_DST_COLOR,VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    if(factor>=12)throw std::runtime_error("Unsupported D3D blend factor");return table[factor];
}
}
void VulkanRenderer::draw(VulkanSurface& target,VulkanSurface* depth,VulkanSurface* texture0,VulkanSurface* texture1,
                         const std::vector<VulkanVertex>& vertices,const std::vector<uint16_t>& indices,const VulkanDrawState& state){
    if(vertices.empty()||indices.empty())return;
    if(!whiteTexture_){whiteTexture_=createSurface(1,1,false,32);uint32_t pixel=UINT32_MAX;uploadSurface(*whiteTexture_,&pixel);}
    if(!texture0)texture0=whiteTexture_.get();if(!texture1)texture1=whiteTexture_.get();
    if(texture0->layout==VK_IMAGE_LAYOUT_UNDEFINED||texture1->layout==VK_IMAGE_LAYOUT_UNDEFINED)throw std::runtime_error("Texture is not initialized");
    if(target.layout==VK_IMAGE_LAYOUT_UNDEFINED)clearSurface(target,0,1);
    if(depth&&depth->layout==VK_IMAGE_LAYOUT_UNDEFINED)clearSurface(*depth,0,1);
    std::array<uint32_t,9> key{state.primitive,state.cull,uint32_t(state.blend),state.sourceBlend,state.destinationBlend,uint32_t(state.zTest),uint32_t(state.zWrite),state.zCompare,uint32_t(depth?depth->format:VK_FORMAT_UNDEFINED)};
    auto found=pipelines_.find(key);bool create=found==pipelines_.end();
    auto cached=create?std::make_shared<VulkanPipeline>():found->second;if(create)cached->device=device_;
    auto& resources=*cached;
    if(pendingDraws_.size()>=128)flushDraws();
    auto batch=std::make_shared<VulkanDrawBatch>(device_,pool_);batch->pipeline=cached;
    auto& upload=batch->upload;
    if(create){
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader.codeSize=sizeof fixed_vert;shader.pCode=fixed_vert;
    check(vkCreateShaderModule(device_,&shader,nullptr,&resources.vertex),"Vertex shader");shader.codeSize=sizeof fixed_frag;shader.pCode=fixed_frag;
    check(vkCreateShaderModule(device_,&shader,nullptr,&resources.fragment),"Fragment shader");
    VkAttachmentDescription attachments[2]{};attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM;attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout=attachments[0].finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    attachments[1]=attachments[0];attachments[1].format=depth?(depth->depth?VK_FORMAT_D16_UNORM:VK_FORMAT_D32_SFLOAT):VK_FORMAT_D16_UNORM;
    // Depth image format is stored explicitly below to support both D16 and D32.
    if(depth)attachments[1].format=depth->format;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},z{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;subpass.pDepthStencilAttachment=depth?&z:nullptr;
    VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
    dependency.srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    dependency.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=depth?2:1;pass.pAttachments=attachments;pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=1;pass.pDependencies=&dependency;
    check(vkCreateRenderPass(device_,&pass,nullptr,&resources.pass),"D3D render pass");
    VkDescriptorSetLayoutBinding bindings[2]{};for(uint32_t i=0;i<2;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;}
    VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptors.bindingCount=2;descriptors.pBindings=bindings;
    check(vkCreateDescriptorSetLayout(device_,&descriptors,nullptr,&resources.descriptorLayout),"Texture bindings");
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,64};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&resources.descriptorLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    check(vkCreatePipelineLayout(device_,&layout,nullptr,&resources.layout),"D3D pipeline layout");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;sampler.maxLod=0;
    check(vkCreateSampler(device_,&sampler,nullptr,&resources.sampler),"Texture sampler");
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,258};VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;pool.maxSets=129;pool.poolSizeCount=1;pool.pPoolSizes=&poolSize;
    check(vkCreateDescriptorPool(device_,&pool,nullptr,&resources.descriptors),"Texture descriptor pool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};setInfo.descriptorPool=resources.descriptors;setInfo.descriptorSetCount=1;setInfo.pSetLayouts=&resources.descriptorLayout;
    check(vkAllocateDescriptorSets(device_,&setInfo,&resources.set),"Texture descriptor set");
    VkPipelineShaderStageCreateInfo stages[2]{};for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=resources.vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=resources.fragment;
    VkVertexInputBindingDescription binding{0,sizeof(VulkanVertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[]={{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32A32_SFLOAT,16},{2,0,VK_FORMAT_R32G32_SFLOAT,32},{3,0,VK_FORMAT_R32G32_SFLOAT,40}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;input.vertexAttributeDescriptionCount=4;input.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    static const VkPrimitiveTopology topologies[]={VK_PRIMITIVE_TOPOLOGY_POINT_LIST,VK_PRIMITIVE_TOPOLOGY_POINT_LIST,VK_PRIMITIVE_TOPOLOGY_LINE_LIST,VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN};
    if(state.primitive>6)throw std::runtime_error("Invalid primitive topology");assembly.topology=topologies[state.primitive];
    VkPipelineViewportStateCreateInfo viewportInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewportInfo.viewportCount=1;viewportInfo.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;
    raster.cullMode=state.cull==1?VK_CULL_MODE_NONE:state.cull==2?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_BACK_BIT;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depthState.depthTestEnable=depth&&state.zTest;depthState.depthWriteEnable=depth&&state.zWrite;
    depthState.depthCompareOp=VkCompareOp(state.zCompare?state.zCompare-1:VK_COMPARE_OP_ALWAYS);
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;blend.blendEnable=state.blend;blend.srcColorBlendFactor=blend.srcAlphaBlendFactor=blendFactor(state.sourceBlend);blend.dstColorBlendFactor=blend.dstAlphaBlendFactor=blendFactor(state.destinationBlend);
    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blendState.attachmentCount=1;blendState.pAttachments=&blend;
    VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamicStates;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipeline.stageCount=2;pipeline.pStages=stages;pipeline.pVertexInputState=&input;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewportInfo;pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&multisample;pipeline.pDepthStencilState=&depthState;pipeline.pColorBlendState=&blendState;pipeline.layout=resources.layout;pipeline.renderPass=resources.pass;pipeline.pDynamicState=&dynamic;
    check(vkCreateGraphicsPipelines(device_,pipelineCache_,1,&pipeline,nullptr,&resources.pipeline),"D3D draw pipeline");
    // Bound the cache without invalidating a pipeline used by this call.
    if(pipelines_.size()>=128){flushDraws();pipelines_.clear();}pipelines_.emplace(key,cached);
    }
    auto& frame=batch->frame;
    VkDescriptorSetAllocateInfo drawSet{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};drawSet.descriptorPool=resources.descriptors;drawSet.descriptorSetCount=1;drawSet.pSetLayouts=&resources.descriptorLayout;
    check(vkAllocateDescriptorSets(device_,&drawSet,&batch->descriptor),"Draw texture snapshot");
    VkImageView views[]={target.view,depth?depth->view:VK_NULL_HANDLE};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=resources.pass;framebuffer.attachmentCount=depth?2:1;framebuffer.pAttachments=views;framebuffer.width=target.width;framebuffer.height=target.height;framebuffer.layers=1;
    check(vkCreateFramebuffer(device_,&framebuffer,nullptr,&frame.handle),"D3D framebuffer");
    VkDescriptorImageInfo images[]={{resources.sampler,texture0->view,VK_IMAGE_LAYOUT_GENERAL},{resources.sampler,texture1->view,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};for(uint32_t i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=batch->descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&images[i];}vkUpdateDescriptorSets(device_,2,writes,0,nullptr);
    VkViewport viewport{state.viewport[0],state.viewport[1],state.viewport[2],state.viewport[3],state.viewport[4],state.viewport[5]};
    VkRect2D scissor{{0,0},{target.width,target.height}};
    uint64_t vertexBytes=vertices.size()*sizeof(VulkanVertex),indexOffset=(vertexBytes+3)&~uint64_t(3),total=indexOffset+indices.size()*2;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=total;buffer.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    check(vkCreateBuffer(device_,&buffer,nullptr,&upload.buffer),"Draw buffer");VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device_,upload.buffer,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&upload.bufferMemory),"Draw memory");check(vkBindBufferMemory(device_,upload.buffer,upload.bufferMemory,0),"Draw binding");check(vkMapMemory(device_,upload.bufferMemory,0,VK_WHOLE_SIZE,0,&upload.mapped),"Draw mapping");
    std::memcpy(upload.mapped,vertices.data(),vertexBytes);std::memcpy(static_cast<uint8_t*>(upload.mapped)+indexOffset,indices.data(),indices.size()*2);
    VkMappedMemoryRange flush{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};flush.memory=upload.bufferMemory;flush.size=VK_WHOLE_SIZE;check(vkFlushMappedMemoryRanges(device_,1,&flush),"Draw visibility");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commands.commandPool=pool_;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;check(vkAllocateCommandBuffers(device_,&commands,&upload.command),"Draw command");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(upload.command,&begin),"Draw begin");
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};render.renderPass=resources.pass;render.framebuffer=frame.handle;render.renderArea=scissor;vkCmdBeginRenderPass(upload.command,&render,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(upload.command,VK_PIPELINE_BIND_POINT_GRAPHICS,resources.pipeline);vkCmdBindDescriptorSets(upload.command,VK_PIPELINE_BIND_POINT_GRAPHICS,resources.layout,0,1,&batch->descriptor,0,nullptr);
    uint8_t constants[64];std::memcpy(constants,state.stages,32);std::memcpy(constants+32,state.alpha,16);std::memcpy(constants+48,state.factor,16);vkCmdPushConstants(upload.command,resources.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,64,constants);
    vkCmdSetViewport(upload.command,0,1,&viewport);vkCmdSetScissor(upload.command,0,1,&scissor);
    VkDeviceSize offset=0;vkCmdBindVertexBuffers(upload.command,0,1,&upload.buffer,&offset);vkCmdBindIndexBuffer(upload.command,upload.buffer,indexOffset,VK_INDEX_TYPE_UINT16);vkCmdDrawIndexed(upload.command,uint32_t(indices.size()),1,0,0,0);vkCmdEndRenderPass(upload.command);
    check(vkEndCommandBuffer(upload.command),"Draw end");VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fence,nullptr,&upload.fence),"Draw fence");
    // Retain command buffers, vertices and descriptor snapshots until the batch
    // completes. Do not stall the CPU for each individual primitive group.
    pendingDraws_.push_back(batch);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&upload.command;check(vkQueueSubmit(queue_,1,&submit,upload.fence),"Draw submit");
}

std::vector<uint8_t> VulkanRenderer::readSurface(VulkanSurface& surface){
    flushDraws();
    if(surface.depth)throw std::runtime_error("Depth readback unsupported");
    TestResources resources{device_,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool_};
    uint64_t bytes=uint64_t(surface.width)*surface.height*4;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=bytes;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device_,&buffer,nullptr,&resources.buffer),"Upload buffer creation");
    VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device_,resources.buffer,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&resources.bufferMemory),"Upload allocation");
    check(vkBindBufferMemory(device_,resources.buffer,resources.bufferMemory,0),"Upload binding");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commands.commandPool=pool_;
    commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&commands,&resources.command),"Upload command allocation");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(resources.command,&begin),"Upload command begin");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=surface.image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.oldLayout=surface.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask=surface.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={surface.width,surface.height,1};
    vkCmdCopyImageToBuffer(resources.command,surface.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,resources.buffer,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(resources.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    check(vkEndCommandBuffer(resources.command),"Upload command end");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fence,nullptr,&resources.fence),"Upload fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&resources.command;
    check(vkQueueSubmit(queue_,1,&submit,resources.fence),"Upload submit");
    check(vkWaitForFences(device_,1,&resources.fence,VK_TRUE,2000000000ULL),"Upload completion");surface.layout=VK_IMAGE_LAYOUT_GENERAL;
    check(vkMapMemory(device_,resources.bufferMemory,0,VK_WHOLE_SIZE,0,&resources.mapped),"Readback mapping");
    VkMappedMemoryRange invalidate{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};invalidate.memory=resources.bufferMemory;invalidate.size=VK_WHOLE_SIZE;
    check(vkInvalidateMappedMemoryRanges(device_,1,&invalidate),"Readback visibility");
    std::vector<uint8_t> pixels(bytes);std::memcpy(pixels.data(),resources.mapped,bytes);return pixels;
}

void VulkanRenderer::testTriangleReadback(){
    auto target=createSurface(64,64,false,32);clearSurface(*target,0xff000000,1);
    VulkanDrawState state;state.cull=1;state.stages[0]=2;state.stages[1]=0;state.stages[3]=2;state.viewport[2]=state.viewport[3]=64;state.viewport[5]=1;
    std::vector<VulkanVertex> vertices(3);float positions[3][4]={{-0.75f,-0.75f,0.5f,1},{0.75f,-0.75f,0.5f,1},{0,0.75f,0.5f,1}};
    for(uint32_t i=0;i<3;++i){std::memcpy(vertices[i].position,positions[i],16);vertices[i].color[1]=vertices[i].color[3]=1;}
    draw(*target,nullptr,nullptr,nullptr,vertices,{0,1,2},state);auto pixels=readSurface(*target);
    uint32_t center=(32*64+32)*4;if(pixels[center]!=0||pixels[center+1]!=255||pixels[center+2]!=0||pixels[center+3]!=255||pixels[0]!=0||pixels[1]!=0||pixels[2]!=0)
        throw std::runtime_error("Vulkan fixed-function triangle readback mismatch");
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","Vulkan 1.1 fixed-function triangle readback passed");
}

void VulkanRenderer::clearRegions(VulkanSurface& target,VulkanSurface* depth,uint32_t flags,uint32_t argb,float value,const std::vector<VkRect2D>& regions){
    flushDraws();
    if(regions.empty())return;
    if(target.layout==VK_IMAGE_LAYOUT_UNDEFINED)clearSurface(target,0,1);
    if(depth&&depth->layout==VK_IMAGE_LAYOUT_UNDEFINED)clearSurface(*depth,0,1);
    VulkanPipeline resources{};resources.device=device_;DrawFramebuffer frame{device_};
    TestResources commands{device_,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,pool_};
    VkAttachmentDescription attachments[2]{};attachments[0].format=target.format;attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout=attachments[0].finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    attachments[1]=attachments[0];if(depth)attachments[1].format=depth->format;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},z{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;subpass.pDepthStencilAttachment=depth?&z:nullptr;
    VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;dependency.srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    dependency.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=depth?2:1;pass.pAttachments=attachments;pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=1;pass.pDependencies=&dependency;
    check(vkCreateRenderPass(device_,&pass,nullptr,&resources.pass),"Region clear pass");
    VkImageView views[]={target.view,depth?depth->view:VK_NULL_HANDLE};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=resources.pass;fb.attachmentCount=depth?2:1;fb.pAttachments=views;fb.width=target.width;fb.height=target.height;fb.layers=1;
    check(vkCreateFramebuffer(device_,&fb,nullptr,&frame.handle),"Region clear framebuffer");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=pool_;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device_,&allocate,&commands.command),"Region clear commands");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commands.command,&begin),"Region clear begin");
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};render.renderPass=resources.pass;render.framebuffer=frame.handle;render.renderArea={{0,0},{target.width,target.height}};
    vkCmdBeginRenderPass(commands.command,&render,VK_SUBPASS_CONTENTS_INLINE);
    VkClearAttachment clear[2]{};uint32_t count=0;
    if(flags&1){auto& attachment=clear[count++];attachment.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        attachment.clearValue.color.float32[0]=((argb>>16)&255)/255.0f;attachment.clearValue.color.float32[1]=((argb>>8)&255)/255.0f;attachment.clearValue.color.float32[2]=(argb&255)/255.0f;attachment.clearValue.color.float32[3]=(argb>>24)/255.0f;}
    if(flags&2){if(!depth)throw std::runtime_error("Depth clear requested without a depth attachment");auto& attachment=clear[count++];attachment.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;attachment.clearValue.depthStencil={value,0};}
    std::vector<VkClearRect> rectangles;for(auto region:regions)rectangles.push_back({region,0,1});
    if(count)vkCmdClearAttachments(commands.command,count,clear,uint32_t(rectangles.size()),rectangles.data());vkCmdEndRenderPass(commands.command);
    check(vkEndCommandBuffer(commands.command),"Region clear end");VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device_,&fence,nullptr,&commands.fence),"Region clear fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&commands.command;
    check(vkQueueSubmit(queue_,1,&submit,commands.fence),"Region clear submit");check(vkWaitForFences(device_,1,&commands.fence,VK_TRUE,2000000000ULL),"Region clear completion");
}
