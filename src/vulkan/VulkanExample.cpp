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

#define VK_USE_PLATFORM_WIN32_KHR
#include <Magnum/Math/Vector3.h>
#include <Magnum/Platform/GlfwApplication.h>

#include <Magnum/Vk/CommandBuffer.h>
#include <Magnum/Vk/CommandPool.h>
#include <Magnum/Vk/Context.h>
#include <Magnum/Vk/Device.h>
#include <Magnum/Vk/PhysicalDevice.h>
#include <Magnum/Vk/Queue.h>
#include <Magnum/Vk/Semaphore.h>
#include <Magnum/Vk/Swapchain.h>

#include <Magnum/Math/Matrix4.h>
#include <Corrade/Containers/Array.h>
#include <Corrade/Utility/Directory.h>

#include <cstring> // for memcpy


namespace Magnum { namespace Examples {

using namespace Vk;

VkPipelineShaderStageCreateInfo loadShader(Device& device, std::string filename, VkShaderStageFlagBits stage) {
    VkResult err;
    VkShaderModule shaderModule;

    if(!Corrade::Utility::Directory::fileExists(filename)) {
        Error() << "File " << filename << "does not exist!";
    }
    Corrade::Containers::Array<char> shaderCode = Corrade::Utility::Directory::read(filename);

    VkShaderModuleCreateInfo moduleCreateInfo;
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.codeSize = shaderCode.size();
    moduleCreateInfo.pCode = reinterpret_cast<uint32_t*>(shaderCode.data());
    moduleCreateInfo.flags = 0;

    err = vkCreateShaderModule(device.vkDevice(), &moduleCreateInfo, nullptr, &shaderModule);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkPipelineShaderStageCreateInfo shaderStage = {};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = stage;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    return shaderStage;
}

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
        std::unique_ptr<Device> _device;
        VkPipelineStageFlags _submitPipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        struct MySemaphores {
            Semaphore presentComplete;
            Semaphore renderComplete;

            MySemaphores():
                presentComplete(NoCreate),
                renderComplete(NoCreate)
            {}
        } _semaphores;

        std::unique_ptr<Queue> _queue;
        std::unique_ptr<CommandPool> _cmdPool;
        std::unique_ptr<CommandBuffer> _setupCmdBuffer;

        VkSurfaceKHR _surface;
        std::unique_ptr<Swapchain> _swapchain;

        struct {
            std::unique_ptr<CommandBuffer> prePresent;
            std::unique_ptr<CommandBuffer> postPresent;
        } _cmdBuffers;

        struct {
            VkImage image;
            VkDeviceMemory mem;
            VkImageView view;
        } _depthStencil;

        VkRenderPass _renderPass;
        std::vector<VkFramebuffer> _frameBuffers;
        VkBuffer _vertexBuffer;
        VkDeviceMemory _vertexBufferMemory;
        VkBuffer _indexBuffer;
        VkDeviceMemory _indexBufferMemory;

        std::vector<std::unique_ptr<CommandBuffer>> _drawCmdBuffers;

        struct {
            Matrix4 projectionMatrix;
            Matrix4 modelMatrix;
            Matrix4 viewMatrix;
        } _uniforms;

        VkBuffer _uniformBuffer;
        VkDeviceMemory _uniformBufferMemory;
        VkDescriptorBufferInfo _uniformDescriptor;
        VkPipelineCache _pipelineCache;
        VkPipelineLayout _pipelineLayout;
        VkPipeline _pipeline;
        VkDescriptorPool _deadpool;
        VkDescriptorSet _descriptorSet;
        VkDescriptorSetLayout _descriptorSetLayout;
        VkPipelineShaderStageCreateInfo _shaderStages[2];

        Float _rot;
        UnsignedInt _currentBuffer;
};

VulkanExample::VulkanExample(const Arguments& arguments)
    : Platform::Application{arguments, Configuration{}.setTitle("Magnum Vulkan Triangle Example")},
      _context{{Vk::Context::Flag::EnableValidation}},
      _swapchain{nullptr},
      _rot{0.0f},
      _currentBuffer{0}
{
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
    PhysicalDevice physicalDevice = PhysicalDevice{physicalDevices[0]};

    UnsignedInt graphicsQueueIndex = physicalDevice.getQueueFamilyIndex(QueueFamily::Graphics);

    // Vulkan device
    float queuePriorities = 0.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriorities;

    _device.reset(new Device{physicalDevice, queueCreateInfo});
    MAGNUM_VK_ASSERT_ERROR(err);

    // Find a suitable depth format
    VkFormat depthFormat = physicalDevice.getSupportedDepthFormat();

    _queue.reset(new Queue{*_device, graphicsQueueIndex, 0});

    _semaphores.presentComplete = Semaphore{*_device};
    _semaphores.renderComplete = Semaphore{*_device};

    _cmdPool.reset(new CommandPool{*_device, QueueFamily::Graphics});

    _setupCmdBuffer = _cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary);
    _setupCmdBuffer->begin();

    _surface = createVkSurface();
    _swapchain.reset(new Swapchain{*_device, *_setupCmdBuffer, _surface});

    _cmdBuffers.prePresent =
            _cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary);
    _cmdBuffers.postPresent =
            _cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary);

    VkImageCreateInfo image = {};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.pNext = nullptr;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = depthFormat;
    image.extent = {800, 600, 1 };
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image.flags = 0;

    VkMemoryAllocateInfo mem_alloc = {};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.pNext = nullptr;
    mem_alloc.allocationSize = 0;
    mem_alloc.memoryTypeIndex = 0;

    VkMemoryRequirements memReqs;

    err = vkCreateImage(_device->vkDevice(), &image, nullptr, &_depthStencil.image);
    MAGNUM_VK_ASSERT_ERROR(err);
    vkGetImageMemoryRequirements(_device->vkDevice(), _depthStencil.image, &memReqs);
    mem_alloc.allocationSize = memReqs.size;
    Debug() << "Memory Requirements:" << memReqs.size;
    mem_alloc.memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Debug() << "Memory type:" << mem_alloc.memoryTypeIndex;
    err = vkAllocateMemory(_device->vkDevice(), &mem_alloc, nullptr, &_depthStencil.mem);
    MAGNUM_VK_ASSERT_ERROR(err);

    err = vkBindImageMemory(_device->vkDevice(), _depthStencil.image, _depthStencil.mem, 0);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    VkImageMemoryBarrier imageMemoryBarrier = {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.pNext = nullptr;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    imageMemoryBarrier.image = _depthStencil.image;
    imageMemoryBarrier.subresourceRange = subresourceRange;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        _setupCmdBuffer->vkCommandBuffer() ,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &imageMemoryBarrier);

    VkImageViewCreateInfo depthStencilView = {};
    depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthStencilView.pNext = nullptr;
    depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthStencilView.format = depthFormat;
    depthStencilView.flags = 0;
    depthStencilView.subresourceRange = {};
    depthStencilView.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depthStencilView.subresourceRange.baseMipLevel = 0;
    depthStencilView.subresourceRange.levelCount = 1;
    depthStencilView.subresourceRange.baseArrayLayer = 0;
    depthStencilView.subresourceRange.layerCount = 1;
    depthStencilView.image = _depthStencil.image;

    err = vkCreateImageView(_device->vkDevice(), &depthStencilView, nullptr, &_depthStencil.view);
    MAGNUM_VK_ASSERT_ERROR(err);

    // setup render pass

    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentDescription attachments[2];
    attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    attachments[0].samples = sampleCount;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format = depthFormat;
    attachments[1].samples = sampleCount;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = nullptr;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pResolveAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depthReference;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = nullptr;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 0;
    renderPassInfo.pDependencies = nullptr;

    err = vkCreateRenderPass(_device->vkDevice(), &renderPassInfo, nullptr, &_renderPass);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkImageView attachmentImages[2];
    // Depth/Stencil attachment is the same for all frame buffers
    attachmentImages[1] = _depthStencil.view;

    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.pNext = nullptr;
    fbCreateInfo.renderPass = _renderPass;
    fbCreateInfo.attachmentCount = 2;
    fbCreateInfo.pAttachments = attachmentImages;
    fbCreateInfo.width = 800;
    fbCreateInfo.height = 600;
    fbCreateInfo.layers = 1;

    // Create frame buffers for every swap chain image
    _frameBuffers.resize(_swapchain->imageCount());
    UnsignedInt i = 0;
    for (auto& fb : _frameBuffers) {
        attachmentImages[0] = _swapchain->imageView(i++);
        VkResult err = vkCreateFramebuffer(_device->vkDevice(), &fbCreateInfo, nullptr, &fb);
        MAGNUM_VK_ASSERT_ERROR(err);
    }

    _setupCmdBuffer->end();
    _queue->submit(*_setupCmdBuffer.get()).waitIdle();

    // Base end

    // prepare vertices
    static constexpr Vector3 vertexData[] {
        { 1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }};
    static constexpr UnsignedInt indices[] {
        0, 1, 2
    };

    VkDeviceMemory iMemory;
    VkBuffer iBuffer;
    VkDeviceMemory vMemory;
    VkBuffer vBuffer;

    VkBufferCreateInfo vBufInfo = {};
    vBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vBufInfo.pNext = nullptr;
    vBufInfo.size = sizeof(vertexData);
    vBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    err = vkCreateBuffer(_device->vkDevice(), &vBufInfo, nullptr, &vBuffer);
    MAGNUM_VK_ASSERT_ERROR(err);

    vkGetBufferMemoryRequirements(_device->vkDevice(), vBuffer, &memReqs);
    MAGNUM_VK_ASSERT_ERROR(err);

    Debug() << "Memory requirements:" << memReqs.size;
    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = nullptr;
    memAlloc.allocationSize = memReqs.size;

    memAlloc.memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    err = vkAllocateMemory(_device->vkDevice(), &memAlloc, nullptr, &vMemory);
    MAGNUM_VK_ASSERT_ERROR(err);

    void* data; // Yay! A void pointer O_o
    err = vkMapMemory(_device->vkDevice(), vMemory, 0, memAlloc.allocationSize, 0, &data);
    MAGNUM_VK_ASSERT_ERROR(err);

    std::memcpy(data, vertexData, vBufInfo.size); // argh, is there something better than this in C++11?
    vkUnmapMemory(_device->vkDevice(), vMemory);

    err = vkBindBufferMemory(_device->vkDevice(), vBuffer, vMemory, 0);
    MAGNUM_VK_ASSERT_ERROR(err);

    vBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    err = vkCreateBuffer(_device->vkDevice(), &vBufInfo, nullptr, &_vertexBuffer);
    MAGNUM_VK_ASSERT_ERROR(err);

    vkGetBufferMemoryRequirements(_device->vkDevice(), _vertexBuffer, &memReqs);
    Debug() << "Memory requirements:" << memReqs.size;
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    err = vkAllocateMemory(_device->vkDevice(), &memAlloc, nullptr, &_vertexBufferMemory);
    MAGNUM_VK_ASSERT_ERROR(err);
    err = vkBindBufferMemory(_device->vkDevice(), _vertexBuffer, _vertexBufferMemory, 0);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkBufferCreateInfo iBufInfo = {};
    iBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    iBufInfo.size = sizeof(indices);
    iBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    err = vkCreateBuffer(_device->vkDevice(), &iBufInfo, nullptr, &iBuffer);
    MAGNUM_VK_ASSERT_ERROR(err);
    vkGetBufferMemoryRequirements(_device->vkDevice(), iBuffer, &memReqs);

    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    err = vkAllocateMemory(_device->vkDevice(), &memAlloc, nullptr, &iMemory);
    MAGNUM_VK_ASSERT_ERROR(err);

    vkMapMemory(_device->vkDevice(), iMemory, 0, memAlloc.allocationSize, 0, &data);
    memcpy(data, indices, iBufInfo.size);
    vkUnmapMemory(_device->vkDevice(), iMemory);

    err = vkBindBufferMemory(_device->vkDevice(), iBuffer, iMemory, 0);
    MAGNUM_VK_ASSERT_ERROR(err);

    iBufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    err = vkCreateBuffer(_device->vkDevice(), &iBufInfo, nullptr, &_indexBuffer);
    MAGNUM_VK_ASSERT_ERROR(err);

    vkGetBufferMemoryRequirements(_device->vkDevice(), _indexBuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    err = vkAllocateMemory(_device->vkDevice(), &memAlloc, nullptr, &_indexBufferMemory);
    MAGNUM_VK_ASSERT_ERROR(err);

    err = vkBindBufferMemory(_device->vkDevice(), _indexBuffer, _indexBufferMemory, 0);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkBufferCopy copyRegion = {};
    std::unique_ptr<CommandBuffer> copyToDeviceCmds = _cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary);
    copyToDeviceCmds->begin();

    copyRegion.size = vBufInfo.size;
    vkCmdCopyBuffer(copyToDeviceCmds->vkCommandBuffer(), vBuffer, _vertexBuffer, 1, &copyRegion);

    copyRegion.size = iBufInfo.size;
    vkCmdCopyBuffer(copyToDeviceCmds->vkCommandBuffer(), iBuffer, _indexBuffer, 1, &copyRegion);

    copyToDeviceCmds->end();

    _queue->submit(*copyToDeviceCmds).waitIdle();

    vkDestroyBuffer(_device->vkDevice(), vBuffer, nullptr);
    vkFreeMemory(_device->vkDevice(), vMemory, nullptr);
    vkDestroyBuffer(_device->vkDevice(), iBuffer, nullptr);
    vkFreeMemory(_device->vkDevice(), iMemory, nullptr);

    VkVertexInputBindingDescription bindingDesc;
    bindingDesc.binding = 0;
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDesc.stride = sizeof(Vector3)*2;

    // Position
    VkVertexInputAttributeDescription attributeDesc[2];
    attributeDesc[0].binding = 0;
    attributeDesc[0].location = 0;
    attributeDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDesc[0].offset = 0;

    // Color
    attributeDesc[1].binding = 0;
    attributeDesc[1].location = 1;
    attributeDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDesc[1].offset = sizeof(Vector3);

    VkPipelineVertexInputStateCreateInfo pvisci = {};
    pvisci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pvisci.pNext = nullptr;
    pvisci.vertexBindingDescriptionCount = 1;
    pvisci.pVertexBindingDescriptions = &bindingDesc;
    pvisci.vertexAttributeDescriptionCount = 2;
    pvisci.pVertexAttributeDescriptions = attributeDesc;

    /* Uniform Buffer */
    VkBufferCreateInfo uBufInfo = {};
    uBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uBufInfo.pNext = nullptr;
    uBufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uBufInfo.size = sizeof(_uniforms);
    err = vkCreateBuffer(_device->vkDevice(), &uBufInfo, nullptr, &_uniformBuffer);
    MAGNUM_VK_ASSERT_ERROR(err);

    vkGetBufferMemoryRequirements(_device->vkDevice(), _uniformBuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    err = vkAllocateMemory(_device->vkDevice(), &memAlloc, nullptr, &_uniformBufferMemory);
    MAGNUM_VK_ASSERT_ERROR(err);

    err = vkBindBufferMemory(_device->vkDevice(), _uniformBuffer, _uniformBufferMemory, 0);
    MAGNUM_VK_ASSERT_ERROR(err);

    _uniformDescriptor.buffer = _uniformBuffer;
    _uniformDescriptor.offset = 0;
    _uniformDescriptor.range = uBufInfo.size;

    // TODO: Clipping Range 0-1
    _uniforms.projectionMatrix = Matrix4::perspectiveProjection(Deg(60.0f), 800.0f/600.0f, 0.1f, 256.0f);
    _uniforms.viewMatrix = Matrix4::translation(Vector3{0.0f, 0.0f, -2.5f});
    _uniforms.modelMatrix = Matrix4{};

    Debug() << "Projection:";
    Debug() << _uniforms.projectionMatrix;

    // map and update the buffers memory
    err = vkMapMemory(_device->vkDevice(), _uniformBufferMemory, 0, uBufInfo.size, 0, &data);
    MAGNUM_VK_ASSERT_ERROR(err);

    memcpy(data, &_uniforms, uBufInfo.size);
    vkUnmapMemory(_device->vkDevice(), _uniformBufferMemory);
    MAGNUM_VK_ASSERT_ERROR(err);

    // setup descriptor set layout
    VkDescriptorSetLayoutBinding layoutBinding = {};
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descLayout = {};
    descLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descLayout.pNext = nullptr;
    descLayout.bindingCount = 1;
    descLayout.pBindings = &layoutBinding;

    err = vkCreateDescriptorSetLayout(_device->vkDevice(), &descLayout, nullptr, &_descriptorSetLayout);
    MAGNUM_VK_ASSERT_ERROR(err);


    VkPipelineLayoutCreateInfo plInfo = {};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.pNext = nullptr;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &_descriptorSetLayout;

    err = vkCreatePipelineLayout(_device->vkDevice(), &plInfo, nullptr, &_pipelineLayout);
    MAGNUM_VK_ASSERT_ERROR(err);

    // Pipeline! :D
    // Vertex input state
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.pNext = nullptr;
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizationState = {};
    rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationState.pNext = nullptr;
    rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = VK_CULL_MODE_NONE;
    rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Not that it matters...
    rasterizationState.depthClampEnable = VK_FALSE;
    rasterizationState.rasterizerDiscardEnable = VK_FALSE;
    rasterizationState.depthBiasEnable = VK_FALSE;
    rasterizationState.lineWidth = 1.0f;

    // blend state
    VkPipelineColorBlendStateCreateInfo blendState = {};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.pNext = nullptr;

    VkPipelineColorBlendAttachmentState blendAttachmentState = {};
    blendAttachmentState.colorWriteMask = 0xf;
    blendAttachmentState.blendEnable = VK_FALSE;

    blendState.attachmentCount = 1;
    blendState.pAttachments = &blendAttachmentState;

    // viewport state
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkDynamicState dynamicStateEnables[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
        2, dynamicStateEnables
    };

    VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
    depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.pNext = nullptr;
    depthStencilState.depthTestEnable = VK_TRUE; // TODO: We don't need this...
    depthStencilState.depthWriteEnable = VK_TRUE; // TODO: We don't need this...
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
    depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
    depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilState.stencilTestEnable = VK_FALSE;
    depthStencilState.front = depthStencilState.back;

    // multi sample state
    VkPipelineMultisampleStateCreateInfo multisampleState = {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0,
        sampleCount,
        VK_FALSE,
        0.0f,
        nullptr,
        VK_FALSE,
        VK_FALSE
    };

    _shaderStages[0] = loadShader(*_device, "./shaders/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    _shaderStages[1] = loadShader(*_device, "./shaders/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.renderPass = _renderPass;
    pipelineInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineInfo.pVertexInputState = &pvisci;
    pipelineInfo.pRasterizationState = &rasterizationState;
    pipelineInfo.pColorBlendState = &blendState;
    pipelineInfo.pMultisampleState = &multisampleState;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pDepthStencilState = &depthStencilState;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = _shaderStages;
    pipelineInfo.pDynamicState = &dynamicState;

    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    err = vkCreatePipelineCache(_device->vkDevice(), &pipelineCacheCreateInfo, nullptr, &_pipelineCache);
    MAGNUM_VK_ASSERT_ERROR(err);

    err = vkCreateGraphicsPipelines(_device->vkDevice(), _pipelineCache, 1, &pipelineInfo, nullptr, &_pipeline);

    // setup descriptor pool and descriptor set
    VkDescriptorPoolSize typeCounts;
    typeCounts.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    typeCounts.descriptorCount = 1;

    VkDescriptorPoolCreateInfo deadpoolInfo = {};
    deadpoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    deadpoolInfo.pNext = nullptr;
    deadpoolInfo.poolSizeCount = 1;
    deadpoolInfo.pPoolSizes = &typeCounts;
    deadpoolInfo.maxSets = 1;

    err = vkCreateDescriptorPool(_device->vkDevice(), &deadpoolInfo, nullptr, &_deadpool);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkDescriptorSetAllocateInfo dSetInfo = {};
    dSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dSetInfo.pNext = nullptr;
    dSetInfo.descriptorPool = _deadpool;
    dSetInfo.descriptorSetCount = 1;
    dSetInfo.pSetLayouts = &_descriptorSetLayout;

    err = vkAllocateDescriptorSets(_device->vkDevice(), &dSetInfo, &_descriptorSet);
    MAGNUM_VK_ASSERT_ERROR(err);

    VkWriteDescriptorSet dSet = {};
    dSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dSet.pNext = nullptr;
    dSet.descriptorCount = 1;
    dSet.dstArrayElement = 0;
    dSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    dSet.pBufferInfo = &_uniformDescriptor;
    dSet.dstBinding = 0;
    dSet.dstSet = _descriptorSet;
    dSet.pImageInfo = nullptr;
    dSet.pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(_device->vkDevice(), 1, &dSet, 0, nullptr);

    // FINALLY! build command buffers
    VkClearValue clearValues[2];
    VkClearColorValue defaultClearColor = {{0.3f, 0.3f, 0.3f, 1.0f}};
    clearValues[0].color = defaultClearColor;
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.pNext = nullptr;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;
    renderPassBeginInfo.renderArea = VkRect2D{{0, 0}, {800, 600}};
    renderPassBeginInfo.renderPass = _renderPass;

    _drawCmdBuffers.resize(_swapchain->imageCount());
    for(UnsignedInt i = 0; i < _drawCmdBuffers.size(); ++i) {
        _drawCmdBuffers[i] = _cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary);
        CommandBuffer& cmdBuffer = *(_drawCmdBuffers[i]);

        renderPassBeginInfo.framebuffer = _frameBuffers[i];

        cmdBuffer.begin();
        vkCmdBeginRenderPass(cmdBuffer.vkCommandBuffer(), &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {0, 0, 800, 600, 0.0f, 1.0f};
        vkCmdSetViewport(cmdBuffer.vkCommandBuffer(), 0, 1, &viewport);

        VkRect2D scissor = {{0, 0}, {800, 600}};
        vkCmdSetScissor(cmdBuffer.vkCommandBuffer(), 0, 1, &scissor);

        vkCmdBindDescriptorSets(cmdBuffer.vkCommandBuffer(),
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);

        vkCmdBindPipeline(cmdBuffer.vkCommandBuffer(),
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _pipeline);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmdBuffer.vkCommandBuffer(), 0, 1, &_vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmdBuffer.vkCommandBuffer(), _indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmdBuffer.vkCommandBuffer(), 3, 1, 0, 0, 1);

        vkCmdEndRenderPass(cmdBuffer.vkCommandBuffer());

        VkImageMemoryBarrier prePresentBarrier = {};
        prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        prePresentBarrier.pNext = nullptr;
        prePresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        prePresentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        prePresentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        prePresentBarrier.image = _swapchain->image(i);

        vkCmdPipelineBarrier(cmdBuffer.vkCommandBuffer(),
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &prePresentBarrier);

        cmdBuffer.end();
    }
}

VulkanExample::~VulkanExample() {
    vkDestroyBuffer(_device->vkDevice(), _vertexBuffer, nullptr);
    vkDestroyBuffer(_device->vkDevice(), _indexBuffer, nullptr);
    vkDestroyBuffer(_device->vkDevice(), _uniformBuffer, nullptr);

    vkDestroyImageView(_device->vkDevice(), _depthStencil.view, nullptr);

    vkDestroyImage(_device->vkDevice(), _depthStencil.image, nullptr);

    vkFreeMemory(_device->vkDevice(), _vertexBufferMemory, nullptr);
    vkFreeMemory(_device->vkDevice(), _indexBufferMemory, nullptr);
    vkFreeMemory(_device->vkDevice(), _uniformBufferMemory, nullptr);
    vkFreeMemory(_device->vkDevice(), _depthStencil.mem, nullptr);

    vkDestroyShaderModule(_device->vkDevice(), _shaderStages[0].module, nullptr);
    vkDestroyShaderModule(_device->vkDevice(), _shaderStages[1].module, nullptr);

    vkDestroyPipelineCache(_device->vkDevice(), _pipelineCache, nullptr);
    vkDestroyPipelineLayout(_device->vkDevice(), _pipelineLayout, nullptr);
    vkDestroyPipeline(_device->vkDevice(), _pipeline, nullptr);

    vkDestroyRenderPass(_device->vkDevice(), _renderPass, nullptr);

    vkFreeDescriptorSets(_device->vkDevice(), _deadpool, 1, &_descriptorSet);
    vkDestroyDescriptorPool(_device->vkDevice(), _deadpool, nullptr);
    vkDestroyDescriptorSetLayout(_device->vkDevice(), _descriptorSetLayout, nullptr);

    for(VkFramebuffer& fb : _frameBuffers) {
        vkDestroyFramebuffer(_device->vkDevice(), fb, nullptr);
    }
}

void VulkanExample::drawEvent() {
    _device->waitIdle();
    _swapchain->acquireNextImage(_semaphores.presentComplete);

    // Add a post present image memory barrier
    // This will transform the frame buffer color attachment back
    // to it's initial layout after it has been presented to the
    // windowing system
    VkImageMemoryBarrier postPresentBarrier = {};
    postPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    postPresentBarrier.pNext = nullptr;
    postPresentBarrier.srcAccessMask = 0;
    postPresentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    postPresentBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    postPresentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    postPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postPresentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    postPresentBarrier.image = _swapchain->image();

    // Use dedicated command buffer from example base class for submitting the post present barrier
    _cmdBuffers.postPresent->begin();

    // Put post present barrier into command buffer
    vkCmdPipelineBarrier(
        _cmdBuffers.postPresent->vkCommandBuffer(),
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &postPresentBarrier);

    _cmdBuffers.postPresent->end();

    _queue->submit(*_cmdBuffers.postPresent);
    // Submit to the graphics queue
    _queue->submit(*_drawCmdBuffers[_swapchain->currentIndex()],
        {_semaphores.presentComplete},
        {_semaphores.renderComplete});

    // Present the current buffer to the swap chain
    // We pass the signal semaphore from the submit info
    // to ensure that the image is not rendered until
    // all commands have been submitted
    _swapchain->queuePresent(*_queue, _swapchain->currentIndex(), _semaphores.renderComplete);
    _device->waitIdle();
}

void VulkanExample::keyPressEvent(KeyEvent& e) {
    if(e.key() == KeyEvent::Key::Esc) {
        exit();
    } else {

    }
}

void VulkanExample::mousePressEvent(MouseEvent&) {
}

void VulkanExample::mouseReleaseEvent(MouseEvent&) {
}

void VulkanExample::mouseMoveEvent(MouseMoveEvent&) {
    _rot += 0.5f;
    _uniforms.modelMatrix = Matrix4::rotationY(Deg(_rot));

    // Map uniform buffer and update it
    void *data;
    VkResult err = vkMapMemory(_device->vkDevice(), _uniformBufferMemory, 0, sizeof(_uniforms), 0, &data);
    MAGNUM_VK_ASSERT_ERROR(err);
    memcpy(data, &_uniforms, sizeof(_uniforms));
    vkUnmapMemory(_device->vkDevice(), _uniformBufferMemory);
    MAGNUM_VK_ASSERT_ERROR(err);
}

void VulkanExample::mouseScrollEvent(MouseScrollEvent&) {
}

}}

MAGNUM_APPLICATION_MAIN(Magnum::Examples::VulkanExample)
