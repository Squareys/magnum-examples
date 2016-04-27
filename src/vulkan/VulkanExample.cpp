/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016
              Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include <Magnum/Math/Vector3.h>
#include <Magnum/Platform/GlfwApplication.h>
#include <Magnum/Vk/Context.h>


namespace Magnum { namespace Examples {

class PhysicalDevice {
    friend class Device;

    public:
        PhysicalDevice() {}

        VkBool32 getSupportedDepthFormat(VkFormat *depthFormat)
        {
            // Since all depth formats may be optional, we need to find a suitable depth format to use
            // Start with the highest precision packed format
            std::vector<VkFormat> depthFormats = {
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM
            };

            for (auto& format : depthFormats)
            {
                VkFormatProperties formatProps;
                vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &formatProps);
                // Format must support depth stencil attachment for optimal tiling
                if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                {
                    *depthFormat = format;
                    return true;
                }
            }

            return false;
        }

        PhysicalDevice(const VkPhysicalDevice& device) {
            _physicalDevice = device;
        }

        UnsignedInt getGraphicsQueueIndex() {
            // Find a queue that supports graphics operations
            uint32_t graphicsQueueIndex = 0;
            uint32_t queueCount;
            vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueCount, nullptr);
            assert(queueCount >= 1);

            std::vector<VkQueueFamilyProperties> queueProps;
            queueProps.resize(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueCount, queueProps.data());

            for (graphicsQueueIndex = 0; graphicsQueueIndex < queueCount; graphicsQueueIndex++)
            {
                if (queueProps[graphicsQueueIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    break;
            }
            assert(graphicsQueueIndex < queueCount);

            return graphicsQueueIndex;
        }

        VkPhysicalDeviceProperties getProperties() {
            VkPhysicalDeviceProperties deviceProperties;
            VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
            vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);

            // Gather physical device memory properties
            vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &deviceMemoryProperties);

            return deviceProperties;
        }

        VkPhysicalDevice& vkPhysicalDevice() {
            return _physicalDevice;
        }

    private:
        VkPhysicalDevice _physicalDevice;
};

class Queue {
    public:

        Queue(const VkQueue& q): _queue(q) {
        }

    private:
        VkQueue _queue;
};

class Semaphore {
    public:

        Semaphore() = default;
        Semaphore(const Semaphore& sem): _semaphore{sem._semaphore} {}

        Semaphore(const VkSemaphore& sem): _semaphore{sem} {

        }

        VkSemaphore operator=(Semaphore& sem) {
            return sem._semaphore;
        }

        Semaphore& operator=(const Semaphore& sem) {
            _semaphore = sem._semaphore;
            return *this;
        }

        VkSemaphore& vkSemaphore() {
            return _semaphore;
        }

    private:
        VkSemaphore _semaphore;
};

class CommandBuffer {
    public:

        enum class Level: Int {
            Primary = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            Secondary = VK_COMMAND_BUFFER_LEVEL_SECONDARY
        };

        CommandBuffer() = default;

        CommandBuffer(VkCommandBuffer buffer): _cmdBuffer{buffer} {
        }

        CommandBuffer& begin() {
            VkCommandBufferBeginInfo cmdBufInfo = {};
            cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            vkBeginCommandBuffer(_cmdBuffer, &cmdBufInfo);

            return *this;
        }

        VkCommandBuffer vkCommandBuffer() {
            return _cmdBuffer;
        }

    private:
        VkCommandBuffer _cmdBuffer;
};

class Device;

class CommandPool {
    public:

        CommandPool() = default;

        CommandPool(VkCommandPool pool): _cmdPool{pool} {
        }

        CommandBuffer allocateCommandBuffer(Device& device, CommandBuffer::Level level);

    private:
        VkCommandPool _cmdPool;
};


class Device {
    public:

        Device(const PhysicalDevice& physicalDevice, VkDeviceQueueCreateInfo requestedQueues, bool enableValidation) {
            std::vector<const char*> enabledExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

            VkDeviceCreateInfo deviceCreateInfo = {};
            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.pNext = nullptr;
            deviceCreateInfo.queueCreateInfoCount = 1;
            deviceCreateInfo.pQueueCreateInfos = &requestedQueues;
            deviceCreateInfo.pEnabledFeatures = nullptr;

            if (!enabledExtensions.empty()) {
                deviceCreateInfo.enabledExtensionCount = UnsignedInt(enabledExtensions.size());
                deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
            }
            if (enableValidation) {
                //deviceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount;
                //deviceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
            }

            vkCreateDevice(physicalDevice._physicalDevice, &deviceCreateInfo, nullptr, &_device);
        }

        ~Device() {
            vkDestroyDevice(_device, nullptr);
        }

        Queue getDeviceQueue(UnsignedInt index) {
            // Get the graphics queue
            VkQueue queue;
            vkGetDeviceQueue(_device, index, 0, &queue);

            return Queue(queue);
        }

        Semaphore createSemaphore() {
            VkSemaphore semaphore;
            // Create synchronization objects
            VkSemaphoreCreateInfo semaphoreCreateInfo = {};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreCreateInfo.pNext = nullptr;
            semaphoreCreateInfo.flags = 0;

            // Create a semaphore used to synchronize image presentation
            // Ensures that the image is displayed before we start submitting new commands to the queu
            vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &semaphore);
            return Semaphore{semaphore};
        }

        CommandPool createCommandPool(const UnsignedInt queueFamilyIndex);

        VkDevice vkDevice() {
            return _device;
        }

    private:
        VkDevice _device;
};

CommandPool Device::createCommandPool(const UnsignedInt queueFamilyIndex){
   VkCommandPool cmdPool;
   VkCommandPoolCreateInfo cmdPoolInfo = {};
   cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
   cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   vkCreateCommandPool(_device, &cmdPoolInfo, nullptr, &cmdPool);

   return CommandPool(cmdPool);
}

CommandBuffer CommandPool::allocateCommandBuffer(Device& device, CommandBuffer::Level level) {
    VkCommandBuffer cmdBuffer;
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = _cmdPool;
    commandBufferAllocateInfo.level = VkCommandBufferLevel(level);
    commandBufferAllocateInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(device.vkDevice(), &commandBufferAllocateInfo, &cmdBuffer);

    return CommandBuffer{cmdBuffer};
}

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint) vk##entrypoint = PFN_vk##entrypoint(vkGetInstanceProcAddr(inst, "vk"#entrypoint))
#define GET_DEVICE_PROC_ADDR(dev, entrypoint) vk##entrypoint = PFN_vk##entrypoint(vkGetDeviceProcAddr(dev, "vk"#entrypoint))

typedef struct {
    VkImage image;
    VkImageView view;
} SwapChainBuffer;

class SwapChain {
    private:
        VkSurfaceKHR surface;
        // Function pointers
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
        PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
        PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
        PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
        PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
        PFN_vkQueuePresentKHR vkQueuePresentKHR;

    public:
        VkFormat colorFormat;
        VkColorSpaceKHR colorSpace;

        VkSwapchainKHR swapChain = VK_NULL_HANDLE;

        uint32_t imageCount;
        std::vector<VkImage> images;
        std::vector<SwapChainBuffer> buffers;

        // Index of the deteced graphics and presenting device queue
        uint32_t queueNodeIndex = UINT32_MAX;

        // Creates an os specific surface
        // Tries to find a graphics and a present queue
        void initSurface(PhysicalDevice& physicalDevice, VkSurfaceKHR surface) {
            this->surface = surface;
            VkResult err;

            // Get available queue family properties
            uint32_t queueCount;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice.vkPhysicalDevice(), &queueCount, nullptr);
            assert(queueCount >= 1);

            std::vector<VkQueueFamilyProperties> queueProps(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice.vkPhysicalDevice(), &queueCount, queueProps.data());

            // Iterate over each queue to learn whether it supports presenting:
            // Find a queue with present support
            // Will be used to present the swap chain images to the windowing system
            std::vector<VkBool32> supportsPresent(queueCount);
            for (uint32_t i = 0; i < queueCount; i++) {
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice.vkPhysicalDevice(), i, surface, &supportsPresent[i]);
            }

            // Search for a graphics and a present queue in the array of queue
            // families, try to find one that supports both
            uint32_t graphicsQueueNodeIndex = UINT32_MAX;
            uint32_t presentQueueNodeIndex = UINT32_MAX;

            for (uint32_t i = 0; i < queueCount; i++) {
                if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                    if (graphicsQueueNodeIndex == UINT32_MAX) {
                        graphicsQueueNodeIndex = i;
                    }

                    if (supportsPresent[i] == VK_TRUE) {
                        graphicsQueueNodeIndex = i;
                        presentQueueNodeIndex = i;
                        break;
                    }
                }
            }

            if (presentQueueNodeIndex == UINT32_MAX) {
                // If there's no queue that supports both present and graphics
                // try to find a separate present queue
                for (uint32_t i = 0; i < queueCount; ++i) {
                    if (supportsPresent[i] == VK_TRUE) {
                        presentQueueNodeIndex = i;
                        break;
                    }
                }
            }

            // Exit if either a graphics or a presenting queue hasn't been found
            if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) {
                return; // fatal
            }

            // todo : Add support for separate graphics and presenting queue
            if (graphicsQueueNodeIndex != presentQueueNodeIndex) {
                return; // fatal
            }

            queueNodeIndex = graphicsQueueNodeIndex;

            // Get list of supported surface formats
            uint32_t formatCount;
            err = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice.vkPhysicalDevice(),
                                                       surface, &formatCount, nullptr);
            assert(!err);
            assert(formatCount > 0);

            std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
            err = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice.vkPhysicalDevice(),
                                                       surface, &formatCount,
                                                       surfaceFormats.data());
            assert(!err);

            // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
            // there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
            if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED)) {
                colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
            } else {
                // Always select the first available color format
                // If you need a specific format (e.g. SRGB) you'd need to
                // iterate over the list of available surface format and
                // check for it's presence
                colorFormat = surfaceFormats[0].format;
            }
            colorSpace = surfaceFormats[0].colorSpace;
        }

        // Connect to the instance und device and get all required function pointers
        void connect(Vk::Context c, PhysicalDevice physicalDevice, Device device) {
            GET_INSTANCE_PROC_ADDR(c.vkInstance(), GetPhysicalDeviceSurfaceSupportKHR);
            GET_INSTANCE_PROC_ADDR(c.vkInstance(), GetPhysicalDeviceSurfaceCapabilitiesKHR);
            GET_INSTANCE_PROC_ADDR(c.vkInstance(), GetPhysicalDeviceSurfaceFormatsKHR);
            GET_INSTANCE_PROC_ADDR(c.vkInstance(), GetPhysicalDeviceSurfacePresentModesKHR);
            GET_DEVICE_PROC_ADDR(device.vkDevice(), CreateSwapchainKHR);
            GET_DEVICE_PROC_ADDR(device.vkDevice(), DestroySwapchainKHR);
            GET_DEVICE_PROC_ADDR(device.vkDevice(), GetSwapchainImagesKHR);
            GET_DEVICE_PROC_ADDR(device.vkDevice(), AcquireNextImageKHR);
            GET_DEVICE_PROC_ADDR(device.vkDevice(), QueuePresentKHR);
        }

        // Create the swap chain and get images with given width and height
        void create(Device device, PhysicalDevice physicalDevice, CommandBuffer& cb) {
            VkResult err;
            VkSwapchainKHR oldSwapchain = swapChain;

            VkCommandBuffer cmdBuffer = cb.vkCommandBuffer();

            // Get physical device surface properties and formats
            VkSurfaceCapabilitiesKHR surfCaps;
            err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice.vkPhysicalDevice(),
                                                            surface, &surfCaps);
            assert(!err);

            // Get available present modes
            uint32_t presentModeCount;
            err = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice.vkPhysicalDevice(),
                                                            surface, &presentModeCount, nullptr);
            assert(!err);
            assert(presentModeCount > 0);

            std::vector<VkPresentModeKHR> presentModes(presentModeCount);

            err = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice.vkPhysicalDevice(),
                                                            surface, &presentModeCount,
                                                            presentModes.data());
            assert(!err);

            VkExtent2D swapchainExtent = {};
            // width and height are either both -1, or both not -1.
            if (surfCaps.currentExtent.width == -1) {
                // If the surface size is undefined, the size is set to
                // the size of the images requested.
                swapchainExtent.width = 640;
                swapchainExtent.height = 480;
            } else {
                // If the surface size is defined, the swap chain size must match
                swapchainExtent = surfCaps.currentExtent;
            }

            // Prefer mailbox mode if present, it's the lowest latency non-tearing present  mode
            VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (size_t i = 0; i < presentModeCount; i++) {
                if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
                if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) && (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
                    swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                }
            }

            // Determine the number of images
            uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
            if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount)) {
                desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
            }

            VkSurfaceTransformFlagsKHR preTransform;
            if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
                preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
            } else {
                preTransform = surfCaps.currentTransform;
            }

            VkSwapchainCreateInfoKHR swapchainCI = {};
            swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapchainCI.pNext = nullptr;
            swapchainCI.surface = surface;
            swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
            swapchainCI.imageFormat = colorFormat;
            swapchainCI.imageColorSpace = colorSpace;
            swapchainCI.imageExtent = { swapchainExtent.width, swapchainExtent.height };
            swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            swapchainCI.preTransform = VkSurfaceTransformFlagBitsKHR(preTransform);
            swapchainCI.imageArrayLayers = 1;
            swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainCI.queueFamilyIndexCount = 0;
            swapchainCI.pQueueFamilyIndices = nullptr;
            swapchainCI.presentMode = swapchainPresentMode;
            swapchainCI.oldSwapchain = oldSwapchain;
            swapchainCI.clipped = true;
            swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

            err = vkCreateSwapchainKHR(device.vkDevice(), &swapchainCI, nullptr, &swapChain);
            assert(!err);

            // If an existing sawp chain is re-created, destroy the old swap chain
            // This also cleans up all the presentable images
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device.vkDevice(), oldSwapchain, nullptr);
            }

            err = vkGetSwapchainImagesKHR(device.vkDevice(), swapChain, &imageCount, nullptr);
            assert(!err);

            // Get the swap chain images
            images.resize(imageCount);
            err = vkGetSwapchainImagesKHR(device.vkDevice(), swapChain, &imageCount, images.data());
            assert(!err);

            // Get the swap chain buffers containing the image and imageview
            buffers.resize(imageCount);
            for (uint32_t i = 0; i < imageCount; i++) {
                VkImageViewCreateInfo colorAttachmentView = {};
                colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                colorAttachmentView.pNext = nullptr;
                colorAttachmentView.format = colorFormat;
                colorAttachmentView.components = {
                    VK_COMPONENT_SWIZZLE_R,
                    VK_COMPONENT_SWIZZLE_G,
                    VK_COMPONENT_SWIZZLE_B,
                    VK_COMPONENT_SWIZZLE_A
                };
                colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                colorAttachmentView.subresourceRange.baseMipLevel = 0;
                colorAttachmentView.subresourceRange.levelCount = 1;
                colorAttachmentView.subresourceRange.baseArrayLayer = 0;
                colorAttachmentView.subresourceRange.layerCount = 1;
                colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
                colorAttachmentView.flags = 0;

                buffers[i].image = images[i];

                VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                VkImageLayout oldImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                VkImageLayout newImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                // Create an image barrier object
                VkImageMemoryBarrier imageMemoryBarrier = {};
                imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageMemoryBarrier.pNext = nullptr;
                // Some default values
                imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageMemoryBarrier.oldLayout = oldImageLayout;
                imageMemoryBarrier.newLayout = newImageLayout;
                imageMemoryBarrier.image = buffers[i].image;

                VkImageSubresourceRange subresourceRange = {};
                subresourceRange.aspectMask = aspectMask;
                subresourceRange.baseMipLevel = 0;
                subresourceRange.levelCount = 1;
                subresourceRange.layerCount = 1;
                imageMemoryBarrier.subresourceRange = subresourceRange;

                // Put barrier on top
                VkPipelineStageFlags srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkPipelineStageFlags destStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

                // Put barrier inside setup command buffer
                vkCmdPipelineBarrier(
                    cmdBuffer,
                    srcStageFlags,
                    destStageFlags,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &imageMemoryBarrier);

                colorAttachmentView.image = buffers[i].image;

                err = vkCreateImageView(device.vkDevice(), &colorAttachmentView, nullptr, &buffers[i].view);
                assert(!err);
            }
        }

        // Acquires the next image in the swap chain
        VkResult acquireNextImage(Device& device, VkSemaphore presentCompleteSemaphore, uint32_t *currentBuffer) {
            return vkAcquireNextImageKHR(device.vkDevice(),
                                         swapChain, UINT64_MAX, presentCompleteSemaphore,
                                         VkFence(nullptr), currentBuffer);
        }

        // Present the current image to the queue
        VkResult queuePresent(VkQueue queue, uint32_t currentBuffer) {
            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.pNext = nullptr;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapChain;
            presentInfo.pImageIndices = &currentBuffer;
            return vkQueuePresentKHR(queue, &presentInfo);
        }

        // Present the current image to the queue
        VkResult queuePresent(VkQueue queue, uint32_t currentBuffer, VkSemaphore waitSemaphore) {
            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.pNext = nullptr;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapChain;
            presentInfo.pImageIndices = &currentBuffer;
            if (waitSemaphore != VK_NULL_HANDLE) {
                presentInfo.pWaitSemaphores = &waitSemaphore;
                presentInfo.waitSemaphoreCount = 1;
            }
            return vkQueuePresentKHR(queue, &presentInfo);
        }

        // Free all Vulkan resources used by the swap chain
        void cleanup(Device& device) {
            for (uint32_t i = 0; i < imageCount; i++) {
                vkDestroyImageView(device.vkDevice(), buffers[i].view, nullptr);
            }
            vkDestroySwapchainKHR(device.vkDevice(), swapChain, nullptr);
            vkDestroySurfaceKHR(Vk::Context::current().vkInstance(), surface, nullptr);
        }
};

class VulkanExample: public Platform::Application {
    public:
        explicit VulkanExample(const Arguments& arguments);
        ~VulkanExample();

    private:
        void drawEvent() override;
        void keyPressEvent(KeyEvent &e) override;
        void mousePressEvent(MouseEvent& event) override;
        void mouseReleaseEvent(MouseEvent& event) override;
        void mouseMoveEvent(MouseMoveEvent& event) override;
        void mouseScrollEvent(MouseScrollEvent& event) override;

        Vk::Context _context;
        PhysicalDevice _physicalDevice;
        std::unique_ptr<Device> _device;
        VkSubmitInfo _submitInfo;
        VkPipelineStageFlags _submitPipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        struct {
                Semaphore presentComplete;
                Semaphore renderComplete;
        } _semaphores;

        CommandPool _cmdPool;
        CommandBuffer _setupCmdBuffer;

        SwapChain _swapChain;

};

VulkanExample::VulkanExample(const Arguments& arguments)
    : Platform::Application{arguments, Configuration{}.setTitle("Magnum Vulkan Triangle Example")},
      _context{Vk::Context::Flag::EnableValidation}
{
    bool enableValidation = false;

    // Physical device
    uint32_t gpuCount = 0;
    // Get number of available physical devices
    VkResult err = vkEnumeratePhysicalDevices(_context.vkInstance(), &gpuCount, nullptr);
    if(err != VK_SUCCESS) {
        Error() << err;
    }
    assert(gpuCount > 0);
    // Enumerate devices
    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
    err = vkEnumeratePhysicalDevices(_context.vkInstance(), &gpuCount, physicalDevices.data());
    if (err)
    {
        Error() << "Could not enumerate phyiscal devices:\n" << err;
        std::exit(8);
    }

    // Note :
    // This example will always use the first physical device reported,
    // change the vector index if you have multiple Vulkan devices installed
    // and want to use another one
    _physicalDevice = PhysicalDevice{physicalDevices[0]};

    UnsignedInt graphicsQueueIndex = _physicalDevice.getGraphicsQueueIndex();

    // Vulkan device
    std::array<float, 1> queuePriorities = { 0.0f };
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = queuePriorities.data();

    _device.reset(new Device{_physicalDevice, queueCreateInfo, enableValidation});
    assert(!err);

    // Find a suitable depth format
    VkFormat depthFormat;
    VkBool32 validDepthFormat = _physicalDevice.getSupportedDepthFormat(&depthFormat);
    assert(validDepthFormat);

    Queue queue = _device->getDeviceQueue(graphicsQueueIndex);

    // swapChain.connect(_context.vkInstance(), _physicalDevice, _device);

    _semaphores.presentComplete = _device->createSemaphore();
    _semaphores.renderComplete = _device->createSemaphore();

    // Set up submit info structure
    // Semaphores will stay the same during application lifetime
    // Command buffer submission info is set by each example
    _submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    _submitInfo.pNext = nullptr;
    _submitInfo.pWaitDstStageMask = &_submitPipelineStages;
    _submitInfo.waitSemaphoreCount = 1;
    _submitInfo.pWaitSemaphores = &_semaphores.presentComplete.vkSemaphore();
    _submitInfo.signalSemaphoreCount = 1;
    _submitInfo.pSignalSemaphores = &_semaphores.renderComplete.vkSemaphore();

    _cmdPool = _device->createCommandPool(0);

    VkSurfaceKHR surface = createVkSurface();

    _setupCmdBuffer = _cmdPool.allocateCommandBuffer(*(_device.get()), CommandBuffer::Level::Primary);
    _setupCmdBuffer.begin();

    _swapChain.initSurface(_physicalDevice, surface);
    _swapChain.create(*_device.get(), _physicalDevice, _setupCmdBuffer);
}


VulkanExample::~VulkanExample() {
}

void VulkanExample::drawEvent() {
}

void VulkanExample::keyPressEvent(KeyEvent& e) {
    if(e.key() == KeyEvent::Key::Esc) {
        exit();
    } else {
        //Debug() << "keyPressEvent:" << char(e.key());
    }
}

void VulkanExample::mousePressEvent(MouseEvent& event) {
    //Debug() << "mousePressEvent:" << int(event.button());
}

void VulkanExample::mouseReleaseEvent(MouseEvent& event) {
    //Debug() << "mouseReleaseEvent:" << int(event.button());
}

void VulkanExample::mouseMoveEvent(MouseMoveEvent& event) {
    //Debug() << "mouseMoveEvent:" << event.position();
}

void VulkanExample::mouseScrollEvent(MouseScrollEvent& event) {
    //Debug() << "mouseScrollEvent:" << event.offset();
}

}}

MAGNUM_APPLICATION_MAIN(Magnum::Examples::VulkanExample)
