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

#include <Magnum/Vk/Buffer.h>
#include <Magnum/Vk/Command.h>
#include <Magnum/Vk/CommandPool.h>
#include <Magnum/Vk/Instance.h>
#include <Magnum/Vk/DescriptorPool.h>
#include <Magnum/Vk/Device.h>
#include <Magnum/Vk/DeviceMemory.h>
#include <Magnum/Vk/Format.h>
#include <Magnum/Vk/Framebuffer.h>
#include <Magnum/Vk/Image.h>
#include <Magnum/Vk/ImageView.h>
#include <Magnum/Vk/Math.h>
#include <Magnum/Vk/Mesh.h>
#include <Magnum/Vk/PhysicalDevice.h>
#include <Magnum/Vk/Pipeline.h>
#include <Magnum/Vk/Queue.h>
#include <Magnum/Vk/RenderPass.h>
#include <Magnum/Vk/Semaphore.h>
#include <Magnum/Vk/Shader.h>
#include <Magnum/Vk/Swapchain.h>

#include <Magnum/Math/Matrix4.h>
#include <Corrade/Containers/Array.h>
#include <Corrade/Utility/Directory.h>

#include <cstring> // for memcpy


namespace Magnum { namespace Examples {

using namespace Vk;

bool loadShader(Vk::Shader& shader, Device& device, std::string filename) {
    if(!Corrade::Utility::Directory::fileExists(filename)) {
        Error() << "File " << filename << "does not exist!";
        return false;
    }
    Corrade::Containers::Array<char> shaderCode = Corrade::Utility::Directory::read(filename);
    shader = {device, shaderCode};
    return true;
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

        Vk::Instance _Instance;
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

        Queue* _queue;
        std::unique_ptr<CommandPool> _cmdPool;
        CommandBuffer _setupCmdBuffer{NoCreate};

        VkSurfaceKHR _surface;
        std::unique_ptr<Swapchain> _swapchain;

        struct {
            CommandBuffer prePresent{NoCreate};
            CommandBuffer postPresent{NoCreate};
        } _cmdBuffers;

        struct {
            std::unique_ptr<Vk::Image> image;
            std::unique_ptr<DeviceMemory> mem;
            std::unique_ptr<Vk::ImageView> view;
        } _depthStencil;

        RenderPass _renderPass{NoCreate};
        std::vector<Vk::Framebuffer> _frameBuffers;

        Vk::Buffer _vertexBuffer{NoCreate};
        std::unique_ptr<DeviceMemory> _vertexBufferMemory;
        Vk::Buffer _indexBuffer{NoCreate};
        std::unique_ptr<DeviceMemory> _indexBufferMemory;

        std::vector<std::unique_ptr<CommandBuffer>> _drawCmdBuffers;

        struct {
            Matrix4 projectionMatrix;
            Matrix4 modelMatrix;
            Matrix4 viewMatrix;
        } _uniforms;

        Vk::Buffer _uniformBuffer{NoCreate};
        std::unique_ptr<DeviceMemory> _uniformBufferMemory;
        VkDescriptorBufferInfo _uniformDescriptor;
        std::unique_ptr<Pipeline> _pipeline;
        std::unique_ptr<DescriptorPool> _deadpool;
        std::unique_ptr<DescriptorSet> _descriptorSet;
        std::unique_ptr<DescriptorSetLayout> _descriptorSetLayout;

        UnsignedInt _currentBuffer;

        Vk::Shader _vertexShader{NoCreate};
        Vk::Shader _fragmentShader{NoCreate};

        Vk::Mesh _triangleMesh;
};

VulkanExample::VulkanExample(const Arguments& arguments)
    : Platform::Application{arguments, Configuration{}.setTitle("Magnum Vulkan Triangle Example")},
      _Instance{{Vk::Instance::Flag::EnableValidation}},
      _currentBuffer{0}
{
    // Note:
    // This example will always use the first physical device reported,
    // change the vector index if you have multiple Vulkan devices installed
    // and want to use another one
    Containers::Array<PhysicalDevice> physicalDevices = _Instance.enumeratePhysicalDevices();
    PhysicalDevice& physicalDevice = physicalDevices[0];

    // Vulkan device
    float queuePriorities = 0.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
        physicalDevice.getQueueFamilyIndex(QueueFamily::Graphics),
        1, &queuePriorities};

    VkPhysicalDeviceFeatures features = physicalDevice.getFeatures();
    _device.reset(new Device{physicalDevice, {queueCreateInfo},
                             {VK_KHR_SWAPCHAIN_EXTENSION_NAME}, {"VK_LAYER_LUNARG_standard_validation"}, features});
    _queue = &_device->getQueue(0);

    // Find a suitable depth format
    VkFormat depthFormat = physicalDevice.getSupportedDepthFormat();

    _semaphores.presentComplete = Semaphore{*_device};
    _semaphores.renderComplete = Semaphore{*_device};

    _cmdPool.reset(new CommandPool{*_device, QueueFamily::Graphics});

    _setupCmdBuffer = std::move(*_cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary).release());
    _setupCmdBuffer.begin();

    _surface = createVkSurface();
    _swapchain.reset(new Swapchain{*_device, _setupCmdBuffer, _surface});

    _cmdBuffers.prePresent = std::move(*_cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary));
    _cmdBuffers.postPresent = std::move(*_cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary));

    _depthStencil.image.reset(new Vk::Image(*_device, Vector3ui{800, 600, 1}, depthFormat, ImageUsageFlag::StencilAttachment | ImageUsageFlag::TransferSrc));

    VkMemoryRequirements memReqs =  _depthStencil.image->getMemoryRequirements();

    UnsignedInt memoryTypeIndex = physicalDevice.getMemoryType(memReqs.memoryTypeBits, MemoryProperty::DeviceLocal);
    _depthStencil.mem.reset(new DeviceMemory{*_device, memReqs.size, memoryTypeIndex});
    _depthStencil.image->bindImageMemory(*_depthStencil.mem);

    ImageMemoryBarrier imageMemoryBarrier{*_depthStencil.image, ImageLayout::Undefined, ImageLayout::DepthStencilAttachmentOptimal,
                VkImageSubresourceRange{
                    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                    0, 1, 0, 1
                }, {}, Access::DepthStencilAttachmentWrite
    };

    _setupCmdBuffer << Cmd::pipelineBarrier(
        PipelineStage::TopOfThePipe,
        PipelineStage::TopOfThePipe,
        {}, {}, {imageMemoryBarrier});

    _depthStencil.view.reset(new Vk::ImageView(*_device, *_depthStencil.image,
                                               depthFormat, VK_IMAGE_VIEW_TYPE_2D,
                                               ImageAspect::Depth | ImageAspect::Stencil));

    // setup render pass

    _renderPass = RenderPass{*_device, depthFormat};

    // Create frame buffers for every swap chain image
    _frameBuffers.reserve(_swapchain->imageCount());
    for (UnsignedInt i = 0; i < _swapchain->imageCount(); ++i) {
        _frameBuffers.emplace_back(Vk::Framebuffer{*_device, _renderPass, Vector3ui{800, 600, 1},
                                                   {_swapchain->imageView(i), *_depthStencil.view}});
    }

    _setupCmdBuffer.end();
    _queue->submit(_setupCmdBuffer)
           .waitIdle();

    // prepare vertices
    static constexpr Vector3 vertexData[] {
        { 1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }};
    static constexpr UnsignedInt indices[] {
        0, 1, 2
    };


    _vertexBuffer = Vk::Buffer{*_device, sizeof(vertexData), Vk::BufferUsage::VertexBuffer | Vk::BufferUsage::TransferDst};
    _vertexBufferMemory = _vertexBuffer.allocateDeviceMemory(MemoryProperty::DeviceLocal);
    _vertexBuffer.update(*_queue, *_cmdPool, vertexData, sizeof(vertexData));

    _indexBuffer = Vk::Buffer{*_device, sizeof(indices), Vk::BufferUsage::IndexBuffer | Vk::BufferUsage::TransferDst};
    _indexBufferMemory = _indexBuffer.allocateDeviceMemory(MemoryProperty::DeviceLocal);
    _indexBuffer.update(*_queue, *_cmdPool, indices, sizeof(indices));

    _triangleMesh.addVertexBuffer(_vertexBuffer);
    _triangleMesh.setIndexBuffer(_indexBuffer);

    /* Uniform Buffer */
    _uniformBuffer = Vk::Buffer{*_device, sizeof(_uniforms), Vk::BufferUsage::UniformBuffer};
    _uniformBufferMemory = _uniformBuffer.allocateDeviceMemory(MemoryProperty::HostVisible);

    _uniformDescriptor = _uniformBuffer.getDescriptor();

    _uniforms.projectionMatrix = Math::perspectiveProjectionZeroToOne<Float>(Deg(60.0f), 800.0f/600.0f, 0.1f, 10.0f);
    _uniforms.viewMatrix = Matrix4::translation(Vector3{0.0f, 0.0f, -2.5f});
    _uniforms.modelMatrix = Matrix4{};

    // map and update the buffers memory
    Containers::ArrayView<char> data = _uniformBufferMemory->map();
    memcpy(data, &_uniforms, _uniformBuffer.size());
    _uniformBufferMemory->unmap();

    // setup descriptor set layout
    _descriptorSetLayout.reset(new DescriptorSetLayout{*_device, {{0, DescriptorType::UniformBuffer, 1, ShaderStage::Vertex}}});

    // Pipeline! :D

    GraphicsPipelineBuilder pipelineBuilder{*_device};

    if(!loadShader(_vertexShader, *_device, "./shaders/triangle.vert.spv")) {
        exit();
    }
    if(!loadShader(_fragmentShader, *_device, "./shaders/triangle.frag.spv")) {
        exit();
    }

    _pipeline = pipelineBuilder
            .setDynamicStates({DynamicState::Viewport, DynamicState::Scissor})
            .addShader(ShaderStage::Vertex, _vertexShader)
            .addShader(ShaderStage::Fragment, _fragmentShader)
            .addDescriptorSetLayout(*_descriptorSetLayout)
            .addVertexInputBinding(0, sizeof(Vector3)*2)
            .addVertexAttributeDescription(0, 0, Format::RGB32_SFLOAT, 0) // position
            .addVertexAttributeDescription(0, 1, Format::RGB32_SFLOAT, sizeof(Vector3)) // color
            .setRenderPass(_renderPass)
            .build();

    // setup descriptor pool and descriptor set
    DescriptorPoolCreateInfo createInfo{*_device};
    createInfo.setPoolSize(DescriptorType::UniformBuffer, 1);

    _deadpool.reset(new DescriptorPool{*_device, 1, createInfo});

    _descriptorSet = _deadpool->allocateDescriptorSet(*_descriptorSetLayout);

    VkWriteDescriptorSet dSet = {};
    dSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dSet.pNext = nullptr;
    dSet.descriptorCount = 1;
    dSet.dstArrayElement = 0;
    dSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    dSet.pBufferInfo = &_uniformDescriptor;
    dSet.dstBinding = 0;
    dSet.dstSet = *_descriptorSet;
    dSet.pImageInfo = nullptr;
    dSet.pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(*_device, 1, &dSet, 0, nullptr);

    // FINALLY! build command buffers
    VkClearValue clearValues[2];
    VkClearColorValue defaultClearColor = {{0.3f, 0.3f, 0.3f, 1.0f}};
    clearValues[0].color = defaultClearColor;
    clearValues[1].depthStencil = {1.0f, 0};

    _drawCmdBuffers.resize(_swapchain->imageCount());
    for(UnsignedInt i = 0; i < _drawCmdBuffers.size(); ++i) {
        _drawCmdBuffers[i] = _cmdPool->allocateCommandBuffer(CommandBuffer::Level::Primary);
        CommandBuffer& cmdBuffer = *(_drawCmdBuffers[i]);

        cmdBuffer.begin()
                 .beginRenderPass(CommandBuffer::SubpassContents::Inline, _renderPass, _frameBuffers[i],
                                  Range2Di{{}, {800, 600}}, {clearValues[0], clearValues[1]});

        cmdBuffer << _pipeline->bindDescriptorSets(BindPoint::Graphics, {*_descriptorSet})
                  << Cmd::setViewport(0, {VkViewport{0, 0, 800, 600, 0.0f, 1.0f}})
                  << Cmd::setScissor(0, {Range2Di{{}, {800, 600}}})
                  << _pipeline->bind(BindPoint::Graphics);

        cmdBuffer << _triangleMesh.draw();

        cmdBuffer.endRenderPass();

        ImageMemoryBarrier prePresentBarrier{_swapchain->image(i),
                    ImageLayout::ColorAttachmentOptimal, ImageLayout::PresentSrc,
                    VkImageSubresourceRange{
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        0, 1, 0, 1
                    }, Access::ColorAttachmentWrite, Access::MemoryRead
        };

        cmdBuffer << Cmd::pipelineBarrier(
                         PipelineStage::AllCommands,
                         PipelineStage::BottomOfPipe,
                         {}, {}, {prePresentBarrier})
                  << Cmd::end();
    }
}

VulkanExample::~VulkanExample() {
}

void VulkanExample::drawEvent() {
    _device->waitIdle();
    _swapchain->acquireNextImage(_semaphores.presentComplete);

    // Add a post present image memory barrier
    // This will transform the frame buffer color attachment back
    // to it's initial layout after it has been presented to the
    // windowing system
    ImageMemoryBarrier postPresentBarrier{_swapchain->image(),
                ImageLayout::PresentSrc, ImageLayout::ColorAttachmentOptimal,
                VkImageSubresourceRange{
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    0, 1, 0, 1
                }, {}, Access::ColorAttachmentWrite
    };

    // Use dedicated command buffer from example base class for submitting the post present barrier
    _cmdBuffers.postPresent << Cmd::begin()
                            << Cmd::pipelineBarrier(
                                   PipelineStage::AllCommands,
                                   PipelineStage::TopOfThePipe,
                                   {}, {}, {postPresentBarrier})
                            << Cmd::end();

    _queue->submit(_cmdBuffers.postPresent)
           .submit(*_drawCmdBuffers[_swapchain->currentIndex()],
                    {_semaphores.presentComplete},
                    {_semaphores.renderComplete});

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

void VulkanExample::mouseMoveEvent(MouseMoveEvent& event) {
    _uniforms.modelMatrix = Matrix4::rotationY(Deg(event.position().x())) * Matrix4::rotationX(Deg(event.position().y()));

    // Map uniform buffer and update it
    Containers::ArrayView<char> data = _uniformBufferMemory->map();
    memcpy(data, &_uniforms, sizeof(_uniforms));
    _uniformBufferMemory->unmap();
}

void VulkanExample::mouseScrollEvent(MouseScrollEvent&) {
}

}}

MAGNUM_APPLICATION_MAIN(Magnum::Examples::VulkanExample)
