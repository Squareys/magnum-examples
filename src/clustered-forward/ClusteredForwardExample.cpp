/*
    This file is part of Magnum.

    Original authors — credit is appreciated but not required:

        2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019 —
            Vladimír Vondruš <mosra@centrum.cz>
        2020 — Jonathan Hale <squareys@googlemail.com>

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or distribute
    this software, either in source code form or as a compiled binary, for any
    purpose, commercial or non-commercial, and by any means.

    In jurisdictions that recognize copyright laws, the author or authors of
    this software dedicate any and all copyright interest in the software to
    the public domain. We make this dedication for the benefit of the public
    at large and to the detriment of our heirs and successors. We intend this
    dedication to be an overt act of relinquishment in perpetuity of all
    present and future rights to this software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <Corrade/Containers/Array.h>
#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/StridedArrayView.h>
#include <Corrade/Containers/Reference.h>
#include <Corrade/Utility/FormatStl.h>
#include <Corrade/PluginManager/PluginManager.h>
#include <Corrade/PluginManager/Manager.h>
#include <Corrade/Utility/DebugStl.h>

#include <Magnum/Image.h>
#include <Magnum/ImageView.h>
#include <Magnum/GL/AbstractShaderProgram.h>
#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Framebuffer.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Shader.h>
#include <Magnum/GL/Texture.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/GL/Version.h>
#include <Magnum/GL/GL.h>
#include <Magnum/DebugTools/FrameProfiler.h>
#include <Magnum/PixelFormat.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/Intersection.h>
#include <Magnum/Math/Frustum.h>
#include <Magnum/Math/Matrix4.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/MeshTools/Concatenate.h>
#include <Magnum/MeshTools/Duplicate.h>
#include <Magnum/MeshTools/GenerateIndices.h>
#include <Magnum/MeshTools/FullScreenTriangle.h>
#include <Magnum/Platform/Sdl2Application.h>
#include <Magnum/Primitives/Cube.h>
#include <Magnum/Primitives/Plane.h>
#include <Magnum/Primitives/UVSphere.h>
#include <Magnum/Shaders/Phong.h>
#include <Magnum/Shaders/Flat.h>
#include <Magnum/Trade/ObjectData3D.h>
#include <Magnum/Trade/MeshObjectData3D.h>
#include <Magnum/Trade/MeshData3D.h>
#include <Magnum/Trade/AbstractImporter.h>

#include "Magnum/GL/AbstractFramebuffer.h"
#include "Magnum/GL/AbstractTexture.h"
#include "Magnum/Math/Vector4.h"
#include "Magnum/Math/Matrix3.h"
#include "Phong.h"

#include <chrono>

#define TILES_X 8
#define TILES_Y 8
#define DEPTH_SLICES 16

namespace Magnum { namespace Examples {

using namespace Magnum::Math::Literals;

Vector3 calculateIntersection(const Vector4 &v1, const Vector4 &v2, const Vector4 &v3) {
    const Float det = Matrix3(v1.xyz(), v2.xyz(), v3.xyz()).determinant();

    const Float x = Matrix3({v1.w(), v2.w(), v3.w()}, {v1.y(), v2.y(), v3.y()}, {v1.z(), v2.z(), v3.z()}).determinant()/det;
    const Float y = Matrix3({v1.x(), v2.x(), v3.x()}, {v1.w(), v2.w(), v3.w()}, {v1.z(), v2.z(), v3.z()}).determinant()/det;
    const Float z = Matrix3({v1.x(), v2.x(), v3.x()}, {v1.y(), v2.y(), v3.y()}, {v1.w(), v2.w(), v3.w()}).determinant()/det;

    return -Vector3{x, y, z};
}

GL::Mesh frustumMesh(const Frustum& frustum) {
    const Vector3 rbn = calculateIntersection(frustum.right(), frustum.bottom(), frustum.near());
    const Vector3 lbn = calculateIntersection(frustum.left(), frustum.bottom(), frustum.near());
    const Vector3 rtn = calculateIntersection(frustum.right(), frustum.top(), frustum.near());
    const Vector3 ltn = calculateIntersection(frustum.left(), frustum.top(), frustum.near());

    const Vector3 rbf = calculateIntersection(frustum.right(), frustum.bottom(), frustum.far());
    const Vector3 lbf = calculateIntersection(frustum.left(), frustum.bottom(), frustum.far());
    const Vector3 rtf = calculateIntersection(frustum.right(), frustum.top(), frustum.far());
    const Vector3 ltf = calculateIntersection(frustum.left(), frustum.top(), frustum.far());

    const Vector3 data[] = {
        rbn, lbn, ltn, rtn,
        rbn, rbf, rtf, rtn,
        rtf, ltf, ltn, ltf,
        lbf, lbn, lbf, rbf
    };

    GL::Buffer buffer;
    buffer.setData(Corrade::Containers::arrayView(data, 16), GL::BufferUsage::StaticDraw);
    GL::Mesh mesh{MeshPrimitive::LineStrip};
    mesh.setCount(16)
        .addVertexBuffer(std::move(buffer), 0, Shaders::Flat3D::Position{});

    return mesh;
}

class DepthShader : public GL::AbstractShaderProgram {
public:

    DepthShader() {
        const GL::Version version = GL::Version::GL330;

        GL::Shader vert{version, GL::Shader::Type::Vertex};
        GL::Shader frag{version, GL::Shader::Type::Fragment};

        vert.addFile(ROOT_DIR "/Depth.vert");
        frag.addFile(ROOT_DIR "/Depth.frag");

        GL::Shader::compile({vert, frag});

        attachShaders({vert, frag});
        link();

        bindFragmentDataLocation(0, "depthSlice");

        scaleUniform = uniformLocation("scale");
        transformationUniform = uniformLocation("transformationMatrix");
        projectionUniform = uniformLocation("projectionMatrix");
        projectionParamsUniform = uniformLocation("projectionParams");
        viewUniform = uniformLocation("viewMatrix");
        planesUniform = uniformLocation("planes");
    }

    DepthShader& setTransformationMatrix(const Matrix4& transformation) {
        setUniform(transformationUniform, transformation);
        return *this;
    }

    DepthShader& setProjectionMatrix(const Matrix4& p) {
        setUniform(projectionUniform, p);
        return *this;
    }

    DepthShader& setViewMatrix(const Matrix4& p) {
        setUniform(viewUniform, p);
        return *this;
    }

    DepthShader& setPlanes(const Containers::ArrayView<const float>& planes) {
        setUniform(planesUniform, Containers::arrayCast<const Vector4>(planes));
        return *this;
    }

    DepthShader& setProjectionParams(float near, float far) {
        const float lfn = std::log2f(far/near);
        const float scale = float(DEPTH_SLICES)/lfn;
        const float offset = float(DEPTH_SLICES)*std::log2f(near)/lfn;
        setUniform(projectionParamsUniform, Vector4{
            near, far, scale, offset});
        return *this;
    }

    int scaleUniform;
    int transformationUniform;
    int projectionUniform;
    int projectionParamsUniform;
    int viewUniform;
    int planesUniform;
};

class ClusterAssignmentShader : public GL::AbstractShaderProgram {
public:

    ClusterAssignmentShader() {
        const GL::Version version = GL::Version::GL330;

        GL::Shader vert{version, GL::Shader::Type::Vertex};
        GL::Shader frag{version, GL::Shader::Type::Fragment};

        vert.addFile(ROOT_DIR "/FullscreenTriangle.vert");
        frag.addSource(Utility::formatString(
            "#define TILES_X {}\n"
            "#define TILES_Y {}\n"
            "#define DEPTH_SLICES {}\n",
            TILES_X, TILES_Y, DEPTH_SLICES));
        frag.addFile(ROOT_DIR "/ClusterAssignment.frag");

        GL::Shader::compile({vert, frag});

        attachShaders({vert, frag});
        link();

        const int depthUniform = uniformLocation("depth");
        setUniform(depthUniform, 0);

        bindFragmentDataLocation(0, "color");

        tanFovUniform = uniformLocation("tanFov");
        viewportScaleUniform = uniformLocation("viewportScale");
        tileSizeUniform = uniformLocation("tileSize");
        inverseProjectionUniform = uniformLocation("inverseProjection");
        projectionParamsUniform = uniformLocation("projectionParams");
    }

    ClusterAssignmentShader& setProjectionParams(float near, float far) {
        const float lfn = std::log2f(far/near);
        const float scale = float(DEPTH_SLICES)/lfn;
        const float offset = float(DEPTH_SLICES)*std::log2f(near)/lfn;
        setUniform(projectionParamsUniform, Vector4{
            near, far, scale, offset});
        return *this;
    }

    ClusterAssignmentShader& setDepthTexture(GL::Texture2D& texture) {
        texture.bind(0);
        return *this;
    }

    ClusterAssignmentShader& setDepthSliceTexture(GL::Texture2D& texture) {
        texture.bind(0);
        return *this;
    }

    ClusterAssignmentShader& setFov(const Deg fov) {
        const float tanFov = Math::tan(0.5*fov);
        setUniform(tanFovUniform, tanFov);
        return *this;
    }

    ClusterAssignmentShader& setViewport(const Vector2& viewport) {
        const Vector2 viewportScale = 1.0f/viewport;
        setUniform(viewportScaleUniform, viewportScale);
        return *this;
    }

    /** Set tile size in screen space */
    ClusterAssignmentShader& setTileSize(const Vector2& tileSize) {
        setUniform(tileSizeUniform, tileSize);
        return *this;
    }

    ClusterAssignmentShader& setProjection(const Matrix4& projection) {
        Matrix4 inverseProjection = projection.inverted();
        setUniform(inverseProjectionUniform, inverseProjection);
        return *this;
    }

    int tanFovUniform;
    int viewportScaleUniform;
    int tileSizeUniform;
    int inverseProjectionUniform;
    int projectionParamsUniform;
};

class ClusteredForwardExample: public Platform::Application {
public:
    explicit ClusteredForwardExample(const Arguments& arguments);

private:
    void drawEvent() override;
    void keyPressEvent(KeyEvent& e) override;
    void keyReleaseEvent(KeyEvent& e) override;
    void mouseMoveEvent(MouseMoveEvent& e) override;
    void mouseScrollEvent(MouseScrollEvent& e) override;
    void reloadShaders();

    GL::Mesh _sphereMesh;
    Shaders::Flat3D _flat;

    Containers::Array<GL::Mesh> _meshes;
    Containers::Array<Matrix4> _transformations;
    DepthShader _depthShader;
    Shaders::Phong _shader;
    // TODO: Configurable num lights
    size_t _numLights = 16*8*2;
    ClusteredForwardPhong _clusteredShader{_numLights, {}, {TILES_X, TILES_Y, DEPTH_SLICES}};

    GL::Framebuffer _depthFramebuffer{NoCreate};
    GL::Framebuffer _clusterKeyFramebuffer{NoCreate};

    GL::Texture2D _depthTexture;
    GL::Texture2D _depthSliceTexture;

    GL::Texture1D _lightListTexture;
    GL::Texture2D _clusterKeyMasks;
    GL::Texture3D _clusterMapTexture;

    DebugTools::GLFrameProfiler _profiler{DebugTools::GLFrameProfiler::Value::CpuDuration|
        DebugTools::GLFrameProfiler::Value::GpuDuration, 50};
    DebugTools::GLFrameProfiler _profilerAssignment{DebugTools::GLFrameProfiler::Value::CpuDuration, 50};
    DebugTools::GLFrameProfiler _profilerCulling{DebugTools::GLFrameProfiler::Value::CpuDuration, 50};
    DebugTools::GLFrameProfiler _profilerRender{DebugTools::GLFrameProfiler::Value::CpuDuration, 50};

    Image2D _clusterKeyMasksImage{PixelFormat::R16UI, {TILES_X, TILES_Y},
        Containers::Array<char>{Containers::ValueInit, sizeof(UnsignedShort)*TILES_X*TILES_Y}};

    Containers::Array<Vector4> _lightPositions{Containers::ValueInit, _numLights};
    Containers::Array<Color4> _lightColors{Containers::ValueInit, _numLights};

    ClusterAssignmentShader _clusterAssignmentShader;

    Containers::Array<UnsignedShort> _lightList{Containers::ValueInit, _numLights*64};
    Image3D _clusterMapImage{PixelFormat::R32UI, {TILES_X, TILES_Y, DEPTH_SLICES},
        Containers::Array<char>{Containers::ValueInit, sizeof(UnsignedInt)*TILES_X*TILES_Y*DEPTH_SLICES}};

    std::chrono::system_clock::time_point _start;
    Vector2 _cameraRotation;
    Vector3 _cameraOffset;
    Vector3 _cameraDirection;
    Float _cameraDistance = 100.0f;
    Matrix4 _view;

    struct {
        bool _debugView = false;
        bool _visualizeCells = false;
        bool _visualizeFrustum = false;
        bool _visualizeLights = false;
        bool _visualizeLightCount = false;
        bool _visualizeClusterKey = false;
        bool _visualizeDepthSlice = false;
        bool _freezeTime = false;
    } _debugOptions;

    float _lastTime = 0.0f;
    int _framesSinceStats = 0;
};

ClusteredForwardExample::ClusteredForwardExample(const Arguments& arguments):
    Platform::Application{arguments,
        Configuration{}.setTitle("Magnum ClusteredForward Example"),
        GLConfiguration{}.addFlags(GLConfiguration::Flag::Debug)}
{
    GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);

    _depthTexture
        .setWrapping({GL::SamplerWrapping::ClampToEdge})
        .setMinificationFilter(GL::SamplerFilter::Nearest)
        .setMagnificationFilter(GL::SamplerFilter::Nearest)
        .setStorage(1, GL::TextureFormat::DepthComponent32F, framebufferSize());
    _depthSliceTexture
        .setWrapping({GL::SamplerWrapping::ClampToEdge})
        .setMinificationFilter(GL::SamplerFilter::Nearest)
        .setMagnificationFilter(GL::SamplerFilter::Nearest)
        .setStorage(1, GL::TextureFormat::R16UI, framebufferSize());
    _depthFramebuffer = GL::Framebuffer({{}, framebufferSize()});
    _depthFramebuffer
        .attachTexture(GL::Framebuffer::BufferAttachment::Depth, _depthTexture, 0)
        .attachTexture(GL::Framebuffer::ColorAttachment(0), _depthSliceTexture, 0);

    _clusterKeyMasks
        .setMagnificationFilter(GL::SamplerFilter::Nearest)
        .setMinificationFilter(GL::SamplerFilter::Nearest, GL::SamplerMipmap::Nearest)
        .setStorage(1, GL::TextureFormat::R16UI, {TILES_X, TILES_Y});
    _clusterKeyFramebuffer = GL::Framebuffer({{}, {TILES_X, TILES_Y}});
    _clusterKeyFramebuffer.attachTexture(GL::Framebuffer::ColorAttachment{0},
        _clusterKeyMasks, 0);

    _clusterMapTexture
        .setMagnificationFilter(GL::SamplerFilter::Nearest)
        .setMinificationFilter(GL::SamplerFilter::Nearest, GL::SamplerMipmap::Nearest)
        .setStorage(1, GL::TextureFormat::R32UI, {TILES_X, TILES_Y, DEPTH_SLICES});

    _lightListTexture
        .setMagnificationFilter(GL::SamplerFilter::Nearest)
        .setMinificationFilter(GL::SamplerFilter::Nearest, GL::SamplerMipmap::Nearest)
        .setStorage(1, GL::TextureFormat::R16UI, int(_lightList.size()));

    int i = 0;
    Vector3 r{80.0f, 150.0f, 50.0f};
    for(int x = 0; x < 16; ++x) {
        for(int y = 0; y < 8; ++y) {
            for(int z = 0; z < 2; ++z) {
                /* Scale to range -20;20 */
                _lightPositions[i] =
                    Vector4{
                        float(x)/4.0f*2*r.x() - r.x() + 0.25f,
                        float(z)/4.0f*r.y(),
                        float(y)/4.0f*2*r.z() - r.z(),
                        3.0f*float(1 + 2*((z+x+y) % 3)) /* Light radius */
                    };
                _lightColors[i] = Color3{float(x)/8.0f, float(y)/8.0f, 0.01f + float(z)/4.0f}.normalized();
                ++i;
            }
        }
    }

    PluginManager::Manager<Trade::AbstractImporter> manager;
    auto importer = manager.loadAndInstantiate("AssimpImporter");
    if(!importer) Fatal() << "Unable to load importer";

    if(!importer->openFile(ROOT_DIR "/assets/sponza.obj"))
        Fatal() << "Unable to load scene";

    for(size_t i = 0; i < importer->object3DCount(); ++i) {
        auto o = importer->object3D(i);

        if(o->instanceType() != Trade::ObjectInstanceType3D::Mesh) continue;

        auto mesh = importer->mesh(o->instance());
        Containers::arrayAppend(_meshes, MeshTools::compile(*mesh));
        Containers::arrayAppend(_transformations, Matrix4::scaling(Vector3{0.2f})*o->transformation());
    }

    _sphereMesh = MeshTools::compile(Primitives::uvSphereWireframe(8, 8));
    _start = std::chrono::system_clock::now();
}

void ClusteredForwardExample::drawEvent() {
    const float time = (_debugOptions._freezeTime) ?
        _lastTime :
        std::chrono::duration_cast<std::chrono::milliseconds>(
            _start - std::chrono::system_clock::now()).count()/1000.0f;
    _lastTime = time;

    GL::defaultFramebuffer.clear(GL::FramebufferClear::Depth|GL::FramebufferClear::Color);
    const Deg fov = 45.0_degf;
    const Float near = 0.1f;
    const Float far = 500.0f;

    const Matrix4 projection = Matrix4::perspectiveProjection(fov, Vector2{windowSize()}.aspectRatio(), near, far);
    const Matrix4 cameraRotationMatrix =
        Matrix4::rotationY(Deg(_cameraRotation.y()))*
        Matrix4::rotationX(Deg(_cameraRotation.x()));
    if(_cameraDirection.x() != 0 || _cameraDirection.z() != 0)
        _cameraOffset += 2.0f*((cameraRotationMatrix.right()*_cameraDirection.x() + cameraRotationMatrix.backward()*_cameraDirection.z()).normalized());

    const Matrix4 debugView =
        (Matrix4::translation(_cameraOffset)*cameraRotationMatrix).inverted();

    /* If debugView is enabled, freeze view (same as last frame) */
    _view = _debugOptions._debugView ? _view : debugView;

    float depthPlanes[DEPTH_SLICES + 1];
    for(int i = 0; i <= DEPTH_SLICES; ++i) {
        depthPlanes[i] = near*Math::pow(far/near, float(i)/DEPTH_SLICES);
    }

    /* 1 Render Scene to GBuffers

       (Depth buffer only in our case, as we do not
       use the normals to fine-tune custers.) */

    _profiler.beginFrame();
    _profilerAssignment.beginFrame();

    _depthFramebuffer.bind();
    _depthFramebuffer.clear(GL::FramebufferClear::Depth);
    _depthFramebuffer.mapForDraw(GL::Framebuffer::ColorAttachment{0});

    _depthShader.setProjectionParams(near, far)
        .setPlanes(Containers::arrayView<float>(depthPlanes, 16))
        .setViewMatrix(_view)
        .setProjectionMatrix(projection);
    for(int i = 0; i < _meshes.size(); ++i) {
        auto& mesh = _meshes[i];
        _depthShader
            .setTransformationMatrix(_transformations[i])
            .draw(mesh);
    }

    /* 2 Cluster Assignment and 3 Find unique clusters
     *
     * Clusters are automatically assigned, since we use a grid: the cluster
     * id is derived from screen-space position of a fragment and its depth.
     *
     * Since we have a fixed number of depth slices (16 for this configuration),
     * we store an unsigned short bitmask per screen space tile (32x32) to indicate
     * whether the associated cluster is being used. */

    GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
    _clusterKeyFramebuffer.mapForDraw(GL::Framebuffer::ColorAttachment(0)).bind();
    _clusterAssignmentShader
        .setDepthSliceTexture(_depthSliceTexture)
        .setProjectionParams(near, far)
        .setViewport(Vector2(framebufferSize()))
        .setTileSize(Vector2(framebufferSize())/Vector2(float(TILES_X), float(TILES_Y)))
        .setProjection(projection)
        .setFov(fov);
    _clusterAssignmentShader.draw(MeshTools::fullScreenTriangle());
    GL::Renderer::enable(GL::Renderer::Feature::DepthTest);

    // Read back pixels from buffer
    _clusterKeyFramebuffer.mapForRead(GL::Framebuffer::ColorAttachment(0));

    // Either:
     //_clusterKeyMasks.image(0, _clusterKeyMasksImage);
    // Or:
    //_clusterKeyFramebuffer.read({{}, {TILES_X, TILES_Y}}, _clusterKeyMasksImage);
    _clusterKeyMasks.image(0, _clusterKeyMasksImage);
    _profilerAssignment.endFrame();

    /* 4 Assign lights to clusters */
    GL::defaultFramebuffer.bind();
    GL::defaultFramebuffer.clear(GL::FramebufferClear::Depth|GL::FramebufferClear::Color);

    _profilerCulling.beginFrame();
    auto start = std::chrono::high_resolution_clock::now();
    int numClusters = 0;


    Frustum frustum = Frustum::fromMatrix(projection*_view);
    for(auto& p : frustum) p /= p.xyz().length();

    Containers::Array<Frustum> cells;
    const Matrix4 v = _view.inverted();
    const Vector3 right = v.right();
    const Vector3 up = v.up();
    const Vector3 fwd = -v.backward();
    const Vector3 o = v.transformPoint({0.0f, 0.0f, 0.0f});

    const float tanFovV = Math::tan(0.5f*fov)/Vector2(windowSize()).aspectRatio();
    const float tanFovH = Math::tan(0.5f*fov);
    const Vector2 tileSize{tanFovH*near/(0.5f*TILES_X), tanFovV*near/(0.5f*TILES_Y)};

    int lightListCount = 0;
    const Vector3 front = o + fwd*near;

    /* Find overlapping lights */
    Containers::Array<Vector4> lights;
    Containers::Array<UnsignedInt> lightIndices;
    for(UnsignedInt i = 0; i < _lightPositions.size(); ++i) {
        const Vector3& sphere = _lightPositions[i].xyz();
        if(Math::Intersection::sphereFrustum(sphere, _lightPositions[i].w(), frustum)) {
            Containers::arrayAppend(lights, _lightPositions[i]);
            Containers::arrayAppend(lightIndices, i);
        }
    }

    Frustum lastCell;
    for(int x = 0; x < TILES_X; ++x) {
        const Vector3 l = front + right*(x - TILES_X/2)*tileSize.x();
        const Vector3 r = front + right*(x - TILES_X/2 + 1)*tileSize.x();
        for(int y = 0; y < TILES_Y; ++y) {
            const Vector3 d = up*(y - TILES_Y/2)*tileSize.y();
            const Vector3 u = up*(y - TILES_Y/2 + 1)*tileSize.y();

            const Vector3 lu = l + u;
            const Vector3 ld = l + d;

            const Vector3 ru = r + u;
            const Vector3 rd = r + d;

            const Vector4 leftPlane = Math::planeEquation(o, ld, lu);
            const Vector4 rightPlane = Math::planeEquation(o, ru, rd);

            const Vector4 bottomPlane = Math::planeEquation(o, rd, ld);
            const Vector4 topPlane = Math::planeEquation(o, lu, ru);

            const UnsignedShort mask = _clusterKeyMasksImage.pixels<UnsignedShort>()[y][x];
            if(mask == 0) continue;

            for(int slice = 0; slice < DEPTH_SLICES; ++slice) {
                /* Check if cluster is used */
                if((mask & (1u << (slice))) == 0) continue;
                ++numClusters;

                const Vector3 n = o + fwd*depthPlanes[slice];
                const Vector3 f = o + fwd*depthPlanes[slice + 1];
                Frustum cell{
                    leftPlane, rightPlane,
                    bottomPlane, topPlane,

                    Math::planeEquation(fwd, n),
                    Math::planeEquation(-fwd, f)
                };

                // TODO: Only need to normalize plane 0-3
                //for(auto& p : frustum) p /= p.xyz().length();

                if(_debugOptions._visualizeCells) {
                    Containers::arrayAppend(cells, cell);
                }
                lastCell = cell;

                // TODO: Only need to normalize plane 0-3
                //for(auto& p : frustum) p /= p.xyz().length();

                //CORRADE_INTERNAL_ASSERT(Math::dot(cell.left().xyz(), frustum.left().xyz()) > 0.0f);
                //CORRADE_INTERNAL_ASSERT(Math::dot(cell.right().xyz(), frustum.right().xyz()) > 0.0f);
                //CORRADE_INTERNAL_ASSERT(Math::dot(cell.bottom().xyz(), frustum.bottom().xyz()) > 0.0f);
                //CORRADE_INTERNAL_ASSERT(Math::dot(cell.top().xyz(), frustum.top().xyz()) > 0.0f);
                //CORRADE_INTERNAL_ASSERT(Math::dot(cell.near().xyz(), frustum.near().xyz()) > 0.0f);
                //CORRADE_INTERNAL_ASSERT(Math::dot(cell.far().xyz(), frustum.far().xyz()) > 0.0f);

                //Debug() << "---";
                //for(auto& p : cell) Debug() << p;

                /* For calculating the num lights for this cell */
                const int lightListOffset = lightListCount;

                /* Find overlapping lights */
                for(size_t i = 0; i < lights.size(); ++i) {
                    const Vector3& sphere = lights[i].xyz();
                    if(Math::Intersection::sphereFrustum(sphere, lights[i].w(), cell)) {
                        _lightList[lightListCount++] = lightIndices[i];
                    }
                }

                /* Set light offset in clusterMap */
                const int lightsCount = lightListCount - lightListOffset;
                _clusterMapImage.pixels<UnsignedInt>()[slice][y][x] =
                    (lightsCount << 24) | lightListOffset;
            }
        }
    }

    _profilerRender.beginFrame();
    _lightListTexture.setSubImage(0, {},
        ImageView1D{PixelFormat::R16UI, lightListCount, _lightList});
    _clusterMapTexture.setSubImage(0, {}, _clusterMapImage);

    /*
    Debug() << "clusters:" << numClusters << ", lights:" << lightListCount << "/" << lights.size() << ", in" <<
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count()/1000.0f << "ms";
    */
    _profilerCulling.endFrame();

    // 5 Shade samples

    _clusteredShader
        .setLights(_lightColors, _lightPositions)
        .setDiffuseColor(Color3{0.9f})
        .setAmbientColor(Color3{0.2f})
        .setProjectionParams(near, far)
        .setProjectionMatrix(projection)
        .setViewMatrix(debugView)
        .bindLightDataTexture(_lightListTexture)
        .bindClusterMapTexture(_clusterMapTexture)
        .setViewport(Vector2(framebufferSize()))
        .setTileSize(Vector2(framebufferSize())/Vector2(float(TILES_X), float(TILES_Y)))
        .setFov(fov);
    for(int i = 0; i < _meshes.size(); ++i) {
        auto& mesh = _meshes[i];
        _clusteredShader
            .setNormalMatrix(_transformations[i].normalMatrix())
            .setTransformationMatrix(_transformations[i])
            .draw(mesh);
    }
    _profilerRender.endFrame();

    if(_debugOptions._visualizeLights) {
        for(const Vector4& sphere : lights) {
            bool visible = Math::Intersection::sphereFrustum(sphere.xyz(), sphere.w(), frustum);
            _flat
                .setColor(visible ? Color4{1.0f, 0.0f, 1.0f, 1.0f} : Color4{0.4f, 0.4f, 0.4f, 1.0f})
                .setTransformationProjectionMatrix(
                    projection*debugView
                    *Matrix4::translation(sphere.xyz())
                    *Matrix4::scaling(Vector3{2*sphere.w()}))
                .draw(_sphereMesh);
        }
    }
    if(_debugOptions._visualizeFrustum) {
        GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
        _flat.setColor(0xff0ffff_rgbaf)
             .setTransformationProjectionMatrix(projection*debugView);
        _flat.draw(frustumMesh(frustum));
        GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    }
    if(_debugOptions._visualizeCells) {
        GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
        _flat.setColor(0xff0ffff_rgbaf)
             .setTransformationProjectionMatrix(projection*debugView);
        for(auto& cell : cells) _flat.draw(frustumMesh(cell));
        GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    }

    /* Clear the cluster map */
    for(int x = 0; x < TILES_X; ++x) {
        for(int y = 0; y < TILES_Y; ++y) {
            if(UnsignedShort mask = _clusterKeyMasksImage.pixels<UnsignedShort>()[y][x]) {
                for(int slice = 0; slice < DEPTH_SLICES; ++slice) {
                    /* Check if cluster is used */
                    if((mask & (1u << slice)) == 0) continue;
                    _clusterMapImage.pixels<UnsignedInt>()[slice][y][x] = 0;
                }
            }
            _clusterKeyMasksImage.pixels<UnsignedShort>()[y][x] = 0u;
        }
    }

    /*
    _clusterKeyFramebuffer.mapForRead(GL::Framebuffer::ColorAttachment{0});
    GL::AbstractFramebuffer::blit(_clusterKeyFramebuffer, GL::defaultFramebuffer,
            Range2Di{{}, {32, 32}}, Range2Di{{}, {128, 128}},
            GL::FramebufferBlit::Color, GL::FramebufferBlitFilter::Nearest);
    */
    _profiler.endFrame();

    if(++_framesSinceStats > 30) {
        Debug() << "Performance";
        Debug() << " Assignment:\t" << _profilerAssignment.statistics();
        Debug() << " Culling:\t" << _profilerCulling.statistics();
        Debug() << " Render:\t" << _profilerRender.statistics();
        Debug() << " Frame:\t" << _profiler.statistics();
        _framesSinceStats = 0;
    }

    swapBuffers();

    redraw();
}

void ClusteredForwardExample::mouseMoveEvent(MouseMoveEvent& e) {
    if(e.buttons() & MouseMoveEvent::Button::Left) {
        _cameraRotation.x() = Math::clamp(_cameraRotation.x() - e.relativePosition().y(), -90.0f, 90.0f);
        _cameraRotation.y() -= e.relativePosition().x();
    }
}

void ClusteredForwardExample::mouseScrollEvent(MouseScrollEvent& e) {
    _cameraDistance = Math::clamp(_cameraDistance - 0.1f*_cameraDistance*e.offset().y(), 0.011f, 200.0f);
}

void ClusteredForwardExample::reloadShaders() {
    /* Reload shaders */
    _clusterAssignmentShader = ClusterAssignmentShader{};
    ClusteredForwardPhong::Flags flags;
    if(_debugOptions._visualizeLightCount) flags |= ClusteredForwardPhong::Flag::VisualizeLightCount;
    if(_debugOptions._visualizeClusterKey) flags |= ClusteredForwardPhong::Flag::VisualizeClusterKey;
    if(_debugOptions._visualizeDepthSlice) flags |= ClusteredForwardPhong::Flag::VisualizeDepthSlice;
    _clusteredShader = ClusteredForwardPhong{_numLights, flags, {TILES_X, TILES_Y, DEPTH_SLICES}};
    _depthShader = DepthShader{};
}

void ClusteredForwardExample::keyReleaseEvent(KeyEvent& e) {
    if(e.key() == KeyEvent::Key::W || e.key() == KeyEvent::Key::S) {
        _cameraDirection.z() = 0;
    } else if(e.key() == KeyEvent::Key::A || e.key() == KeyEvent::Key::D) {
        _cameraDirection.x() = 0;
    }
}

void ClusteredForwardExample::keyPressEvent(KeyEvent& e) {
    if(e.key() == KeyEvent::Key::Esc) exit();
    if(e.key() == KeyEvent::Key::F5) {
        reloadShaders();
    }
    if(e.key() == KeyEvent::Key::W) {
        _cameraDirection.z() = -1;
    }
    if(e.key() == KeyEvent::Key::S) {
        _cameraDirection.z() = 1;
    }
    if(e.key() == KeyEvent::Key::A) {
        _cameraDirection.x() = -1;
    }
    if(e.key() == KeyEvent::Key::D) {
        _cameraDirection.x() = 1;
    }
    if(e.key() == KeyEvent::Key::V) {
        _debugOptions._debugView = !_debugOptions._debugView;
    }
    if(e.key() == KeyEvent::Key::C) {
        _debugOptions._visualizeCells = !_debugOptions._visualizeCells;
    }
    if(e.key() == KeyEvent::Key::F) {
        _debugOptions._visualizeFrustum = !_debugOptions._visualizeFrustum;
    }
    if(e.key() == KeyEvent::Key::L) {
        _debugOptions._visualizeLights = !_debugOptions._visualizeLights;
    }
    if(e.key() == KeyEvent::Key::Space) {
        _debugOptions._freezeTime = !_debugOptions._freezeTime;
    }
    if(e.key() == KeyEvent::Key::One) {
        _debugOptions._visualizeLightCount = false;
        _debugOptions._visualizeClusterKey = false;
        _debugOptions._visualizeDepthSlice = false;
        reloadShaders();
    }
    if(e.key() == KeyEvent::Key::Two) {
        _debugOptions._visualizeLightCount = true;
        _debugOptions._visualizeClusterKey = false;
        _debugOptions._visualizeDepthSlice = false;
        reloadShaders();
    }
    if(e.key() == KeyEvent::Key::Three) {
        _debugOptions._visualizeLightCount = false;
        _debugOptions._visualizeClusterKey = true;
        _debugOptions._visualizeDepthSlice = false;
        reloadShaders();
    }
    if(e.key() == KeyEvent::Key::Four) {
        _debugOptions._visualizeLightCount = false;
        _debugOptions._visualizeClusterKey = false;
        _debugOptions._visualizeDepthSlice = true;
        reloadShaders();
    }
}

}}

MAGNUM_APPLICATION_MAIN(Magnum::Examples::ClusteredForwardExample)
