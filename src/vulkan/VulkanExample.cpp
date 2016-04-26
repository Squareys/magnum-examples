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
            vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueCount, NULL);
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

class CommandPool {
    public:

        CommandPool(VkCommandPool pool): _cmdPool{pool} {
        }

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
            semaphoreCreateInfo.pNext = NULL;
            semaphoreCreateInfo.flags = 0;

            // Create a semaphore used to synchronize image presentation
            // Ensures that the image is displayed before we start submitting new commands to the queu
            vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &semaphore);
            return Semaphore{semaphore};
        }

        CommandPool createCommandPool() {
            VkCommandPoolCreateInfo cmdPoolInfo = {};
            cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
            cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VkResult vkRes = vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool);

            return CommandPool(cmdPool);
        }

    private:
        VkDevice _device;
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
    _submitInfo.pNext = NULL;
    _submitInfo.pWaitDstStageMask = &_submitPipelineStages;
    _submitInfo.waitSemaphoreCount = 1;
    _submitInfo.pWaitSemaphores = &_semaphores.presentComplete.vkSemaphore();
    _submitInfo.signalSemaphoreCount = 1;
    _submitInfo.pSignalSemaphores = &_semaphores.renderComplete.vkSemaphore();

    _device.createCommandPool();
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
