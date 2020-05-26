#ifndef Magnum_ClusteredForwardExample_ClusteredForwardPhong_h
#define Magnum_ClusteredForwardExample_ClusteredForwardPhong_h
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

#include "Magnum/GL/AbstractShaderProgram.h"
#include "Magnum/GL/Buffer.h"
#include "Magnum/Math/Angle.h"
#include "Magnum/Math/Functions.h"
#include "Magnum/Math/Vector2.h"
#include "Magnum/Math/Vector3.h"
#include "Magnum/Math/Vector4.h"
#include "Magnum/Shaders/Generic.h"
#include "Magnum/Shaders/visibility.h"

#define DEPTH_SLICES 16

namespace Magnum { namespace Examples {

class ClusteredForwardPhong: public GL::AbstractShaderProgram {
    public:
        /**
         * @brief Flag
         *
         * @see @ref Flags, @ref flags()
         */
        enum class Flag: UnsignedShort {
            /**
             * Multiply ambient color with a texture.
             * @see @ref setAmbientColor(), @ref bindAmbientTexture()
             */
            AmbientTexture = 1 << 0,

            /**
             * Multiply diffuse color with a texture.
             * @see @ref setDiffuseColor(), @ref bindDiffuseTexture()
             */
            DiffuseTexture = 1 << 1,

            /**
             * Multiply specular color with a texture.
             * @see @ref setSpecularColor(), @ref bindSpecularTexture()
             */
            SpecularTexture = 1 << 2,

            /**
             * Modify normals according to a texture. Requires the
             * @ref Tangent attribute to be present.
             * @m_since{2019,10}
             */
            NormalTexture = 1 << 4,

            /**
             * Enable alpha masking. If the combined fragment color has an
             * alpha less than the value specified with @ref setAlphaMask(),
             * given fragment is discarded.
             *
             * This uses the @glsl discard @ce operation which is known to have
             * considerable performance impact on some platforms. While useful
             * for cheap alpha masking that doesn't require depth sorting,
             * with proper depth sorting and blending you'll usually get much
             * better performance and output quality.
             */
            AlphaMask = 1 << 3,

            /**
             * Multiply diffuse color with a vertex color. Requires either
             * the @ref Color3 or @ref Color4 attribute to be present.
             * @m_since{2019,10}
             */
            VertexColor = 1 << 5,

            #ifndef MAGNUM_TARGET_GLES2
            /**
             * Enable object ID output. See @ref Shaders-ClusteredForwardPhong-usage-object-id
             * for more information.
             * @requires_gles30 Object ID output requires integer buffer
             *      attachments, which are not available in OpenGL ES 2.0 or
             *      WebGL 1.0.
             * @m_since{2019,10}
             */
            ObjectId = 1 << 6,
            #endif

            VisualizeLightCount = 1 << 7,
            VisualizeClusterKey = 1 << 8,
            VisualizeDepthSlice = 1 << 9,
        };

        /**
         * @brief Flags
         *
         * @see @ref flags()
         */
        typedef Containers::EnumSet<Flag> Flags;

        explicit ClusteredForwardPhong(size_t lightCount, Flags flags, const Vector3i& clusterSize);

        /** @brief Copying is not allowed */
        ClusteredForwardPhong(const ClusteredForwardPhong&) = delete;

        /** @brief Move constructor */
        ClusteredForwardPhong(ClusteredForwardPhong&&) noexcept = default;

        /** @brief Copying is not allowed */
        ClusteredForwardPhong& operator=(const ClusteredForwardPhong&) = delete;

        /** @brief Move assignment */
        ClusteredForwardPhong& operator=(ClusteredForwardPhong&&) noexcept = default;

        /** @brief Flags */
        Flags flags() const { return _flags; }

        /** @brief Light count */
        UnsignedInt lightCount() const { return _lightCount; }

        /**
         * @brief Set ambient color
         * @return Reference to self (for method chaining)
         *
         * If @ref Flag::AmbientTexture is set, default value is
         * @cpp 0xffffffff_rgbaf @ce and the color will be multiplied with
         * ambient texture, otherwise default value is @cpp 0x00000000_rgbaf @ce.
         * @see @ref bindAmbientTexture()
         */
        ClusteredForwardPhong& setAmbientColor(const Magnum::Color4& color);

        /**
         * @brief Bind an ambient texture
         * @return Reference to self (for method chaining)
         *
         * Expects that the shader was created with @ref Flag::AmbientTexture
         * enabled.
         * @see @ref bindTextures(), @ref setAmbientColor()
         */
        ClusteredForwardPhong& bindAmbientTexture(GL::Texture2D& texture);

        /**
         * @brief Set diffuse color
         * @return Reference to self (for method chaining)
         *
         * Initial value is @cpp 0xffffffff_rgbaf @ce. If @ref lightCount() is
         * zero, this function is a no-op, as diffuse color doesn't contribute
         * to the output in that case.
         * @see @ref bindDiffuseTexture()
         */
        ClusteredForwardPhong& setDiffuseColor(const Magnum::Color4& color);

        /**
         * @brief Bind a diffuse texture
         * @return Reference to self (for method chaining)
         *
         * Expects that the shader was created with @ref Flag::DiffuseTexture
         * enabled. If @ref lightCount() is zero, this function is a no-op, as
         * diffuse color doesn't contribute to the output in that case.
         * @see @ref bindTextures(), @ref setDiffuseColor()
         */
        ClusteredForwardPhong& bindDiffuseTexture(GL::Texture2D& texture);

        /**
         * @brief Bind a normal texture
         * @return Reference to self (for method chaining)
         * @m_since{2019,10}
         *
         * Expects that the shader was created with @ref Flag::NormalTexture
         * enabled and the @ref Tangent attribute was supplied. If
         * @ref lightCount() is zero, this function is a no-op, as normals
         * dosn't contribute to the output in that case.
         * @see @ref bindTextures()
         */
        ClusteredForwardPhong& bindNormalTexture(GL::Texture2D& texture);

        /**
         * @brief Set specular color
         * @return Reference to self (for method chaining)
         *
         * Initial value is @cpp 0xffffffff_rgbaf @ce. Color will be multiplied
         * with specular texture if @ref Flag::SpecularTexture is set. If you
         * want to have a fully diffuse material, set specular color to
         * @cpp 0x000000ff_rgbaf @ce. If @ref lightCount() is zero, this
         * function is a no-op, as specular color doesn't contribute to the
         * output in that case.
         * @see @ref bindSpecularTexture()
         */
        ClusteredForwardPhong& setSpecularColor(const Magnum::Color4& color);

        /**
         * @brief Bind a specular texture
         * @return Reference to self (for method chaining)
         *
         * Expects that the shader was created with @ref Flag::SpecularTexture
         * enabled. If @ref lightCount() is zero, this function is a no-op, as
         * specular color doesn't contribute to the output in that case.
         * @see @ref bindTextures(), @ref setSpecularColor()
         */
        ClusteredForwardPhong& bindSpecularTexture(GL::Texture2D& texture);

        /**
         * @brief Bind textures
         * @return Reference to self (for method chaining)
         *
         * A particular texture has effect only if particular texture flag from
         * @ref ClusteredForwardPhong::Flag "Flag" is set, you can use @cpp nullptr @ce for the
         * rest. Expects that the shader was created with at least one of
         * @ref Flag::AmbientTexture, @ref Flag::DiffuseTexture,
         * @ref Flag::SpecularTexture or @ref Flag::NormalTexture enabled. More
         * efficient than setting each texture separately.
         * @see @ref bindAmbientTexture(), @ref bindDiffuseTexture(),
         *      @ref bindSpecularTexture(), @ref bindNormalTexture()
         */
        ClusteredForwardPhong& bindTextures(GL::Texture2D* ambient, GL::Texture2D* diffuse, GL::Texture2D* specular, GL::Texture2D* normal
            #ifdef MAGNUM_BUILD_DEPRECATED
            = nullptr
            #endif
        );

        /**
         * @brief Set shininess
         * @return Reference to self (for method chaining)
         *
         * The larger value, the harder surface (smaller specular highlight).
         * Initial value is @cpp 80.0f @ce. If @ref lightCount() is zero, this
         * function is a no-op, as specular color doesn't contribute to the
         * output in that case.
         */
        ClusteredForwardPhong& setShininess(Float shininess);

        /**
         * @brief Set alpha mask value
         * @return Reference to self (for method chaining)
         *
         * Expects that the shader was created with @ref Flag::AlphaMask
         * enabled. Fragments with alpha values smaller than the mask value
         * will be discarded. Initial value is @cpp 0.5f @ce. See the flag
         * documentation for further information.
         */
        ClusteredForwardPhong& setAlphaMask(Float mask);

        #ifndef MAGNUM_TARGET_GLES2
        /**
         * @brief Set object ID
         * @return Reference to self (for method chaining)
         *
         * Expects that the shader was created with @ref Flag::ObjectId
         * enabled. Value set here is written to the @ref ObjectIdOutput, see
         * @ref Shaders-ClusteredForwardPhong-usage-object-id for more information. Default is
         * @cpp 0 @ce.
         * @requires_gles30 Object ID output requires integer buffer
         *      attachments, which are not available in OpenGL ES 2.0 or WebGL
         *      1.0.
         */
        ClusteredForwardPhong& setObjectId(UnsignedInt id);
        #endif

        /**
         * @brief Set transformation matrix
         * @return Reference to self (for method chaining)
         *
         * You need to set also @ref setNormalMatrix() with a corresponding
         * value. Initial value is an identity matrix.
         */
        ClusteredForwardPhong& setTransformationMatrix(const Matrix4& matrix);

        ClusteredForwardPhong& setViewMatrix(const Matrix4& matrix);

        /**
         * @brief Set normal matrix
         * @return Reference to self (for method chaining)
         *
         * The matrix doesn't need to be normalized, as the renormalization
         * must be done in the shader anyway. You need to set also
         * @ref setTransformationMatrix() with a corresponding value. Initial
         * value is an identity matrix. If @ref lightCount() is zero, this
         * function is a no-op, as normals don't contribute to the output in
         * that case.
         * @see @ref Math::Matrix4::normalMatrix()
         */
        ClusteredForwardPhong& setNormalMatrix(const Matrix3x3& matrix);

        /**
         * @brief Set projection matrix
         * @return Reference to self (for method chaining)
         *
         * Initial value is an identity matrix (i.e., an orthographic
         * projection of the default @f$ [ -\boldsymbol{1} ; \boldsymbol{1} ] @f$
         * cube).
         */
        ClusteredForwardPhong& setProjectionMatrix(const Matrix4& matrix);

        ClusteredForwardPhong& setLights(Containers::ArrayView<const Color4> colors, Containers::ArrayView<const Vector4> positions);

        ClusteredForwardPhong& bindClusterMapTexture(GL::Texture3D& texture);
        ClusteredForwardPhong& bindLightDataTexture(GL::Texture1D& texture);

        ClusteredForwardPhong& setFov(const Deg fov);
        ClusteredForwardPhong& setViewport(const Vector2& viewport);
        ClusteredForwardPhong& setTileSize(const Vector2& tileSize);

        ClusteredForwardPhong& setProjectionParams(float near, float far) {
            setUniform(_projectionParamsUniform, Vector2{near, far});
            return *this;
        }

    private:
        Flags _flags;
        UnsignedInt _lightCount;
        Int _transformationMatrixUniform{0},
            _projectionMatrixUniform{1},
            _viewMatrixUniform{0},
            _normalMatrixUniform{2},
            _ambientColorUniform{4},
            _diffuseColorUniform{5},
            _specularColorUniform{6},
            _shininessUniform{7},
            _alphaMaskUniform{8},
            _tanFovUniform{9},
            _viewportScaleUniform{10},
            _tileSizeUniform{11},
            _projectionParamsUniform;
            #ifndef MAGNUM_TARGET_GLES2
            Int _objectIdUniform{12};
            #endif
        Int _lightPositionsUniform{13},
            _lightColorsUniform; /* 10 + lightCount, set in the constructor */

        GL::Buffer _lightsUBO;
};

/** @debugoperatorclassenum{ClusteredForwardPhong,ClusteredForwardPhong::Flag} */
MAGNUM_SHADERS_EXPORT Debug& operator<<(Debug& debug, ClusteredForwardPhong::Flag value);

/** @debugoperatorclassenum{ClusteredForwardPhong,ClusteredForwardPhong::Flags} */
MAGNUM_SHADERS_EXPORT Debug& operator<<(Debug& debug, ClusteredForwardPhong::Flags value);

CORRADE_ENUMSET_OPERATORS(ClusteredForwardPhong::Flags)

}}

#endif
