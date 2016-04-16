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

#include <Magnum/Buffer.h>
#include <Magnum/DefaultFramebuffer.h>
#include <Magnum/Mesh.h>
#include <Magnum/Math/Vector3.h>
#include <Magnum/Platform/GlfwApplication.h>
#include <Magnum/Shaders/VertexColor.h>

namespace Magnum { namespace Examples {

class VulkanExample: public Platform::Application {
    public:
        explicit VulkanExample(const Arguments& arguments);

    private:
        void drawEvent() override;
        void keyPressEvent(KeyEvent &e) override;
        void mousePressEvent(MouseEvent& event) override;
        void mouseReleaseEvent(MouseEvent& event) override;
        void mouseMoveEvent(MouseMoveEvent& event) override;
        void mouseScrollEvent(MouseScrollEvent& event) override;

        Buffer _buffer;
        Mesh _mesh;
        Shaders::VertexColor3D _shader;
};

VulkanExample::VulkanExample(const Arguments& arguments): Platform::Application{arguments, Configuration{}.setTitle("Magnum Vulkan Triangle Example")} {
    constexpr static const Vector3 data[] = {
        {-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, /* Left vertex, red color */
        { 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, /* Right vertex, green color */
        { 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}  /* Top vertex, blue color */
    };

    _buffer.setData(data, BufferUsage::StaticDraw);
    _mesh.setPrimitive(MeshPrimitive::Triangles)
        .setCount(3)
        .addVertexBuffer(_buffer, 0, Shaders::VertexColor3D::Position{}, Shaders::VertexColor3D::Color{});
}

void VulkanExample::drawEvent() {
    defaultFramebuffer.clear(FramebufferClear::Color);

    _mesh.draw(_shader);

    swapBuffers();
}

void VulkanExample::keyPressEvent(KeyEvent& e) {
    if(e.key() == KeyEvent::Key::Esc) {
        exit();
    } else {
        Debug() << "keyPressEvent:" << char(e.key());
    }
}

void VulkanExample::mousePressEvent(MouseEvent& event) {
    Debug() << "mousePressEvent:" << int(event.button());
}

void VulkanExample::mouseReleaseEvent(MouseEvent& event) {
    Debug() << "mouseReleaseEvent:" << int(event.button());
}

void VulkanExample::mouseMoveEvent(MouseMoveEvent& event) {
    Debug() << "mouseMoveEvent:" << event.position();
}

void VulkanExample::mouseScrollEvent(MouseScrollEvent& event) {
    Debug() << "mouseScrollEvent:" << event.offset();
}

}}

MAGNUM_APPLICATION_MAIN(Magnum::Examples::VulkanExample)
