/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019
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

#include "Phong.h"

#include <Magnum/Shaders/Generic.h>

#ifdef MAGNUM_TARGET_GLES
#include <Corrade/Containers/Array.h>
#endif
#include <Corrade/Containers/EnumSet.hpp>
#include <Corrade/Containers/Reference.h>
#include <Corrade/Utility/FormatStl.h>
#include <Corrade/Utility/Resource.h>

#include "Magnum/GL/Context.h"
#include "Magnum/GL/Extensions.h"
#include "Magnum/GL/Shader.h"
#include "Magnum/GL/Texture.h"
#include "Magnum/Math/Color.h"
#include "Magnum/Math/Matrix4.h"

namespace Magnum { namespace Examples {

namespace {
    enum: Int {
        AmbientTextureLayer = 0,
        DiffuseTextureLayer = 1,
        SpecularTextureLayer = 2,
        NormalTextureLayer = 3,
        ClusterMapLayer = 4,
        LightDataLayer = 5
    };
}

ClusteredForwardPhong::ClusteredForwardPhong(const size_t lightCount, const Flags flags, const Vector3i& clusterSize): _flags{flags}, _lightCount{UnsignedInt(lightCount)} {
    #ifdef MAGNUM_BUILD_STATIC
    /* Import resources on static build, if not already */
    if(!Utility::Resource::hasGroup("MagnumShaders"))
        importShaderResources();
    #endif
    Utility::Resource rs("MagnumShaders");

    #ifndef MAGNUM_TARGET_GLES
    const GL::Version version = GL::Context::current().supportedVersion({GL::Version::GL320, GL::Version::GL310, GL::Version::GL300, GL::Version::GL210});
    #else
    const GL::Version version = GL::Context::current().supportedVersion({GL::Version::GLES300, GL::Version::GLES200});
    #endif

    GL::Shader vert{version, GL::Shader::Type::Vertex};
    GL::Shader frag{version, GL::Shader::Type::Fragment};

    vert.addSource(flags & (Flag::AmbientTexture|Flag::DiffuseTexture|Flag::SpecularTexture|Flag::NormalTexture) ? "#define TEXTURED\n" : "")
        .addSource(flags & Flag::NormalTexture ? "#define NORMAL_TEXTURE\n" : "")
        .addSource(flags & Flag::VertexColor ? "#define VERTEX_COLOR\n" : "")
        .addSource(rs.get("generic.glsl"))
        .addFile(ROOT_DIR "/Phong.vert");
    frag.addSource(flags & Flag::AmbientTexture ? "#define AMBIENT_TEXTURE\n" : "")
        .addSource(flags & Flag::DiffuseTexture ? "#define DIFFUSE_TEXTURE\n" : "")
        .addSource(flags & Flag::SpecularTexture ? "#define SPECULAR_TEXTURE\n" : "")
        .addSource(flags & Flag::NormalTexture ? "#define NORMAL_TEXTURE\n" : "")
        .addSource(flags & Flag::VertexColor ? "#define VERTEX_COLOR\n" : "")
        .addSource(flags & Flag::AlphaMask ? "#define ALPHA_MASK\n" : "")
        #ifndef MAGNUM_TARGET_GLES2
        .addSource(flags & Flag::ObjectId ? "#define OBJECT_ID\n" : "")
        #endif
        .addSource(flags & Flag::VisualizeLightCount ? "#define VIZ_LIGHT_COUNT\n" : "")
        .addSource(flags & Flag::VisualizeClusterKey ? "#define VIZ_CLUSTER_KEY\n" : "")
        .addSource(flags & Flag::VisualizeDepthSlice ? "#define VIZ_DEPTH_SLICE\n" : "")
        ;
    Debug() << "Compiled Phong with light count" << lightCount;
    frag.addSource(rs.get("generic.glsl"))
        .addSource(Utility::formatString(
            "#define LIGHT_COUNT {}\n"
            "#define TILES_X {}\n"
            "#define TILES_Y {}\n"
            "#define DEPTH_SLICES {}\n",
            lightCount, clusterSize.x(), clusterSize.y(), clusterSize.z()))
        .addFile(ROOT_DIR "/Phong.frag");

    CORRADE_INTERNAL_ASSERT_OUTPUT(GL::Shader::compile({vert, frag}));

    attachShaders({vert, frag});

    /* ES3 has this done in the shader directly and doesn't even provide
       bindFragmentDataLocation() */
    #if !defined(MAGNUM_TARGET_GLES) || defined(MAGNUM_TARGET_GLES2)
    #ifndef MAGNUM_TARGET_GLES
    if(!GL::Context::current().isExtensionSupported<GL::Extensions::ARB::explicit_attrib_location>(version))
    #endif
    {
        bindAttributeLocation(Shaders::Generic3D::Position::Location, "position");
        bindAttributeLocation(Shaders::Generic3D::Normal::Location, "normal");
        if((flags & Flag::NormalTexture))
            bindAttributeLocation(Shaders::Generic3D::Tangent::Location, "tangent");
        if(flags & Flag::VertexColor)
            bindAttributeLocation(Shaders::Generic3D::Color3::Location, "vertexColor"); /* Color4 is the same */
        if(flags & (Flag::AmbientTexture|Flag::DiffuseTexture|Flag::SpecularTexture))
            bindAttributeLocation(Shaders::Generic3D::TextureCoordinates::Location, "textureCoordinates");
        #ifndef MAGNUM_TARGET_GLES2
        if(flags & Flag::ObjectId) {
            bindFragmentDataLocation(Shaders::Generic3D::ColorOutput, "color");
            bindFragmentDataLocation(Shaders::Generic3D::ObjectIdOutput, "objectId");
        }
        #endif
    }
    #endif

    CORRADE_INTERNAL_ASSERT_OUTPUT(link());

    _transformationMatrixUniform = uniformLocation("transformationMatrix");
    _viewMatrixUniform = uniformLocation("viewMatrix");
    _projectionMatrixUniform = uniformLocation("projectionMatrix");
    _ambientColorUniform = uniformLocation("ambientColor");

    _normalMatrixUniform = uniformLocation("normalMatrix");
    _diffuseColorUniform = uniformLocation("diffuseColor");
    _specularColorUniform = uniformLocation("specularColor");
    _shininessUniform = uniformLocation("shininess");
    _lightPositionsUniform = uniformLocation("lightPositions");
    _lightColorsUniform = uniformLocation("lightColors");

    _tanFovUniform = uniformLocation("tanFov");
    _viewportScaleUniform = uniformLocation("viewportScale");
    _tileSizeUniform = uniformLocation("tileSize");
    _projectionParamsUniform = uniformLocation("projectionParams");

    if(flags & Flag::AlphaMask) _alphaMaskUniform = uniformLocation("alphaMask");
    #ifndef MAGNUM_TARGET_GLES2
    if(flags & Flag::ObjectId) _objectIdUniform = uniformLocation("objectId");
    #endif

    #ifndef MAGNUM_TARGET_GLES
    if(flags && !GL::Context::current().isExtensionSupported<GL::Extensions::ARB::shading_language_420pack>(version))
    #endif
    {
        if(flags & Flag::AmbientTexture) setUniform(uniformLocation("ambientTexture"), AmbientTextureLayer);
        if(flags & Flag::DiffuseTexture) setUniform(uniformLocation("diffuseTexture"), DiffuseTextureLayer);
        if(flags & Flag::SpecularTexture) setUniform(uniformLocation("specularTexture"), SpecularTextureLayer);
        if(flags & Flag::NormalTexture) setUniform(uniformLocation("normalTexture"), NormalTextureLayer);
        setUniform(uniformLocation("clusterMapTexture"), ClusterMapLayer);
        setUniform(uniformLocation("lightDataTexture"), LightDataLayer);
    }

    _lightsUBO.setData({nullptr, lightCount*sizeof(Vector4)*2},
        GL::BufferUsage::DynamicDraw);
    setUniformBlockBinding(uniformBlockIndex("Lights"), 0);
}

ClusteredForwardPhong& ClusteredForwardPhong::setAmbientColor(const Magnum::Color4& color) {
    setUniform(_ambientColorUniform, color);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindAmbientTexture(GL::Texture2D& texture) {
    CORRADE_ASSERT(_flags & Flag::AmbientTexture,
        "Shaders::ClusteredForwardPhong::bindAmbientTexture(): the shader was not created with ambient texture enabled", *this);
    texture.bind(AmbientTextureLayer);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setDiffuseColor(const Magnum::Color4& color) {
    if(_lightCount) setUniform(_diffuseColorUniform, color);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindDiffuseTexture(GL::Texture2D& texture) {
    CORRADE_ASSERT(_flags & Flag::DiffuseTexture,
        "Shaders::ClusteredForwardPhong::bindDiffuseTexture(): the shader was not created with diffuse texture enabled", *this);
    if(_lightCount) texture.bind(DiffuseTextureLayer);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setSpecularColor(const Magnum::Color4& color) {
    if(_lightCount) setUniform(_specularColorUniform, color);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindSpecularTexture(GL::Texture2D& texture) {
    CORRADE_ASSERT(_flags & Flag::SpecularTexture,
        "Shaders::ClusteredForwardPhong::bindSpecularTexture(): the shader was not created with specular texture enabled", *this);
    if(_lightCount) texture.bind(SpecularTextureLayer);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindNormalTexture(GL::Texture2D& texture) {
    CORRADE_ASSERT(_flags & Flag::NormalTexture,
        "Shaders::ClusteredForwardPhong::bindNormalTexture(): the shader was not created with normal texture enabled", *this);
    if(_lightCount) texture.bind(NormalTextureLayer);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindClusterMapTexture(GL::Texture3D& texture) {
    texture.bind(ClusterMapLayer);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindLightDataTexture(GL::Texture1D& texture) {
    texture.bind(LightDataLayer);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::bindTextures(GL::Texture2D* ambient, GL::Texture2D* diffuse, GL::Texture2D* specular, GL::Texture2D* normal) {
    CORRADE_ASSERT(_flags & (Flag::AmbientTexture|Flag::DiffuseTexture|Flag::SpecularTexture|Flag::NormalTexture),
        "Shaders::ClusteredForwardPhong::bindTextures(): the shader was not created with any textures enabled", *this);
    GL::AbstractTexture::bind(AmbientTextureLayer, {ambient, diffuse, specular, normal});
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setShininess(Float shininess) {
    if(_lightCount) setUniform(_shininessUniform, shininess);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setAlphaMask(Float mask) {
    CORRADE_ASSERT(_flags & Flag::AlphaMask,
        "Shaders::ClusteredForwardPhong::setAlphaMask(): the shader was not created with alpha mask enabled", *this);
    setUniform(_alphaMaskUniform, mask);
    return *this;
}

#ifndef MAGNUM_TARGET_GLES2
ClusteredForwardPhong& ClusteredForwardPhong::setObjectId(UnsignedInt id) {
    CORRADE_ASSERT(_flags & Flag::ObjectId,
        "Shaders::ClusteredForwardPhong::setObjectId(): the shader was not created with object ID enabled", *this);
    setUniform(_objectIdUniform, id);
    return *this;
}
#endif

ClusteredForwardPhong& ClusteredForwardPhong::setViewMatrix(const Matrix4& matrix) {
    setUniform(_viewMatrixUniform, matrix);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setTransformationMatrix(const Matrix4& matrix) {
    setUniform(_transformationMatrixUniform, matrix);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setNormalMatrix(const Matrix3x3& matrix) {
    if(_lightCount) setUniform(_normalMatrixUniform, matrix);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setProjectionMatrix(const Matrix4& matrix) {
    setUniform(_projectionMatrixUniform, matrix);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setLights(Containers::ArrayView<const Color4> colors, Containers::ArrayView<const Vector4> positions) {
    _lightsUBO.setSubData(0, colors);
    _lightsUBO.setSubData(_lightCount*sizeof(Color4), positions);

    _lightsUBO.bind(GL::Buffer::Target::Uniform, 0);

    return *this;
}

Debug& operator<<(Debug& debug, const ClusteredForwardPhong::Flag value) {
    debug << "Shaders::ClusteredForwardPhong::Flag" << Debug::nospace;

    switch(value) {
        /* LCOV_EXCL_START */
        #define _c(v) case ClusteredForwardPhong::Flag::v: return debug << "::" #v;
        _c(AmbientTexture)
        _c(DiffuseTexture)
        _c(SpecularTexture)
        _c(NormalTexture)
        _c(AlphaMask)
        _c(VertexColor)
        #ifndef MAGNUM_TARGET_GLES2
        _c(ObjectId)
        #endif
        _c(VisualizeClusterKey)
        _c(VisualizeDepthSlice)
        _c(VisualizeLightCount)
        #undef _c
        /* LCOV_EXCL_STOP */
    }

    return debug << "(" << Debug::nospace << reinterpret_cast<void*>(UnsignedByte(value)) << Debug::nospace << ")";
}

Debug& operator<<(Debug& debug, const ClusteredForwardPhong::Flags value) {
    return Containers::enumSetDebugOutput(debug, value, "Shaders::ClusteredForwardPhong::Flags{}", {
        ClusteredForwardPhong::Flag::AmbientTexture,
        ClusteredForwardPhong::Flag::DiffuseTexture,
        ClusteredForwardPhong::Flag::SpecularTexture,
        ClusteredForwardPhong::Flag::NormalTexture,
        ClusteredForwardPhong::Flag::AlphaMask,
        ClusteredForwardPhong::Flag::VertexColor,
        #ifndef MAGNUM_TARGET_GLES2
        ClusteredForwardPhong::Flag::ObjectId,
        #endif
        ClusteredForwardPhong::Flag::VisualizeClusterKey,
        ClusteredForwardPhong::Flag::VisualizeDepthSlice,
        ClusteredForwardPhong::Flag::VisualizeLightCount
        });
}

ClusteredForwardPhong& ClusteredForwardPhong::setFov(const Deg fov) {
    const float tanFov = Math::tan(0.5*fov);
    setUniform(_tanFovUniform, tanFov);
    return *this;
}

ClusteredForwardPhong& ClusteredForwardPhong::setViewport(const Vector2& viewport) {
    const Vector2 viewportScale = 1.0f/viewport;
    setUniform(_viewportScaleUniform, viewportScale);
    return *this;
}

/** Set tile size in screen space */
ClusteredForwardPhong& ClusteredForwardPhong::setTileSize(const Vector2& tileSize) {
    setUniform(_tileSizeUniform, tileSize);
    return *this;
}

}}
