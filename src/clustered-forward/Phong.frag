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
#if (!defined(GL_ES) && __VERSION__ >= 130) || (defined(GL_ES) && __VERSION__ >= 300)
    #define NEW_GLSL
#endif

#if !defined(GL_ES) && defined(GL_ARB_explicit_attrib_location) && !defined(DISABLE_GL_ARB_explicit_attrib_location)
    #extension GL_ARB_explicit_attrib_location: enable
    #define EXPLICIT_ATTRIB_LOCATION
#endif

#if !defined(GL_ES) && defined(GL_ARB_shading_language_420pack) && !defined(DISABLE_GL_ARB_shading_language_420pack)
    #extension GL_ARB_shading_language_420pack: enable
    #define RUNTIME_CONST
    #define EXPLICIT_TEXTURE_LAYER
#endif

#if !defined(GL_ES) && defined(GL_ARB_explicit_uniform_location) && !defined(DISABLE_GL_ARB_explicit_uniform_location)
    #extension GL_ARB_explicit_uniform_location: enable
    #define EXPLICIT_UNIFORM_LOCATION
#endif

#if defined(GL_ES) && __VERSION__ >= 300
    #define EXPLICIT_ATTRIB_LOCATION
    /* EXPLICIT_TEXTURE_LAYER, EXPLICIT_UNIFORM_LOCATION and RUNTIME_CONST is not
       available in OpenGL ES */
#endif

/* Precision qualifiers are not supported in GLSL 1.20 */
#if !defined(GL_ES) && __VERSION__ == 120
    #define highp
    #define mediump
    #define lowp
#endif

#ifndef NEW_GLSL
#define in varying
#define fragmentColor gl_FragColor
#define texture texture2D
#endif

#ifndef RUNTIME_CONST
#define const
#endif

#ifdef AMBIENT_TEXTURE
#ifdef EXPLICIT_TEXTURE_LAYER
layout(binding = 0)
#endif
uniform lowp sampler2D ambientTexture;
#endif

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 4)
#endif
uniform lowp vec4 ambientColor
    #ifndef GL_ES
    #ifndef AMBIENT_TEXTURE
    = vec4(0.0)
    #else
    = vec4(1.0)
    #endif
    #endif
    ;

#ifdef DIFFUSE_TEXTURE
#ifdef EXPLICIT_TEXTURE_LAYER
layout(binding = 1)
#endif
uniform lowp sampler2D diffuseTexture;
#endif

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 5)
#endif
uniform lowp vec4 diffuseColor
    #ifndef GL_ES
    = vec4(1.0)
    #endif
    ;

#ifdef SPECULAR_TEXTURE
#ifdef EXPLICIT_TEXTURE_LAYER
layout(binding = 2)
#endif
uniform lowp sampler2D specularTexture;
#endif

#ifdef NORMAL_TEXTURE
#ifdef EXPLICIT_TEXTURE_LAYER
layout(binding = 3)
#endif
uniform lowp sampler2D normalTexture;
#endif

#ifdef EXPLICIT_TEXTURE_LAYER
layout(binding = 4)
#endif
uniform lowp usampler3D clusterMapTexture;

#ifdef EXPLICIT_TEXTURE_LAYER
layout(binding = 5)
#endif
uniform usampler1D lightDataTexture;

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 6)
#endif
uniform lowp vec4 specularColor
    #ifndef GL_ES
    = vec4(1.0)
    #endif
    ;

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 7)
#endif
uniform mediump float shininess
    #ifndef GL_ES
    = 80.0
    #endif
    ;

#ifdef ALPHA_MASK
#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 8)
#endif
uniform lowp float alphaMask
    #ifndef GL_ES
    = 0.5
    #endif
    ;
#endif

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 9)
#endif
uniform float tanFov;

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 10)
#endif

uniform vec2 viewportScale;
#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 11)
#endif
uniform vec2 tileSize;

uniform vec2 projectionParams;

#ifdef OBJECT_ID
#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 12)
#endif
/* mediump is just 2^10, which might not be enough, this is 2^16 */
uniform highp uint objectId; /* defaults to zero */
#endif

uniform Lights {
    vec4 lightColors[LIGHT_COUNT];
    vec4 lightPositions[LIGHT_COUNT];
};

in mediump vec3 transformedNormal;
#ifdef NORMAL_TEXTURE
in mediump vec3 transformedTangent;
#endif
in highp vec3 worldPos;

#if defined(AMBIENT_TEXTURE) || defined(DIFFUSE_TEXTURE) || defined(SPECULAR_TEXTURE) || defined(NORMAL_TEXTURE)
in mediump vec2 interpolatedTextureCoords;
#endif

#ifdef VERTEX_COLOR
in lowp vec4 interpolatedVertexColor;
#endif

#ifdef NEW_GLSL
#ifdef EXPLICIT_ATTRIB_LOCATION
layout(location = COLOR_OUTPUT_ATTRIBUTE_LOCATION)
#endif
out mediump vec4 fragmentColor;
#endif
#ifdef OBJECT_ID
#ifdef EXPLICIT_ATTRIB_LOCATION
layout(location = OBJECT_ID_OUTPUT_ATTRIBUTE_LOCATION)
#endif
/* mediump is just 2^10, which might not be enough, this is 2^16 */
out highp uint fragmentObjectId;
#endif

void main() {
    lowp const vec4 finalAmbientColor =
        #ifdef AMBIENT_TEXTURE
        texture(ambientTexture, interpolatedTextureCoords)*
        #endif
        ambientColor;
    lowp const vec4 finalDiffuseColor =
        #ifdef DIFFUSE_TEXTURE
        texture(diffuseTexture, interpolatedTextureCoords)*
        #endif
        #ifdef VERTEX_COLOR
        interpolatedVertexColor*
        #endif
        diffuseColor;
    lowp const vec4 finalSpecularColor =
        #ifdef SPECULAR_TEXTURE
        texture(specularTexture, interpolatedTextureCoords)*
        #endif
        specularColor;

    /* Ambient color */
    fragmentColor = finalAmbientColor;

    /* Normal */
    mediump vec3 normalizedTransformedNormal = normalize(transformedNormal);
    #ifdef NORMAL_TEXTURE
    mediump vec3 normalizedTransformedTangent = normalize(transformedTangent);
    mediump mat3 tbn = mat3(
        normalizedTransformedTangent,
        normalize(cross(normalizedTransformedNormal,
                        normalizedTransformedTangent)),
        normalizedTransformedNormal
    );
    normalizedTransformedNormal = tbn*(texture(normalTexture, interpolatedTextureCoords).rgb*2.0 - vec3(1.0));
    #endif

    vec2 screenPos = gl_FragCoord.xy;

    const highp float near = projectionParams.x;
    const highp float far = projectionParams.y;
    const highp float depthSlices = DEPTH_SLICES;
    const highp float depth = near + gl_FragCoord.z*(far - near);
    const highp float scale = depthSlices/log(far/near);
    const highp float offset = depthSlices*log(near)/log(far/near);

    uint depthSlice = uint(log(depth)*scale - offset);
    vec3 clusterKey = vec3(floor(screenPos/tileSize), depthSlice);

    uint clusterMap = texture(clusterMapTexture, (clusterKey + vec3(0.5))/vec3(TILES_X, TILES_Y, DEPTH_SLICES)).r;
    uint lightOffset = (clusterMap & 0xFFFFFFu);
    uint lightCount = ((clusterMap >> 24u) & 0xFFu);

    /* Add diffuse color for each light */
    for(uint i = 0u; i < lightCount; ++i) {
        uint lightIndex = texture(lightDataTexture, float(lightOffset + i)/(LIGHT_COUNT*64)).r;
        vec4 lightPosition = lightPositions[lightIndex];

        highp vec3 lightDir = lightPosition.xyz - worldPos;
        const float lightRadius = lightPosition.w;
        vec3 l = lightDir/lightRadius;
        float lightDist = length(lightDir);
        const float attenuation = 1.0/(1.0 + 2*lightDist/lightRadius + dot(l, l));
        highp vec3 normalizedLightDirection = normalize(lightDir);

        //const float lightIntensity = lightColors[i].a;
        lowp float intensity = max(0.0, 4.0*attenuation*dot(normalizedTransformedNormal, normalizedLightDirection));
        lowp vec3 lightColor = lightColors[lightIndex].rgb;
        fragmentColor += vec4(finalDiffuseColor.rgb*lightColor*intensity, finalDiffuseColor.a);
    }

    #ifdef VIZ_LIGHT_COUNT
    /* Light count visualization */
    fragmentColor.rgb = lightCount == 0u ? vec3(0, 0, 0) : mix(vec3(0, 1, 0), vec3(1, 0, 0), float(lightCount)/8.0f);
    #endif

    #ifdef VIZ_DEPTH_SLICE
    /* Depth Slice visualization */
    fragmentColor.rgb = vec3[](
        vec3(1, 0, 0),
        vec3(1, 1, 0),
        vec3(0, 1, 0),
        vec3(0, 1, 1),
        vec3(0, 0, 1),
        vec3(1, 1, 1),
        vec3(1, 0, 0),
        vec3(1, 1, 0),
        vec3(0, 1, 0),
        vec3(0, 1, 1),
        vec3(0, 0, 1),
        vec3(1, 1, 1),
        vec3(1, 0, 0),
        vec3(1, 1, 0),
        vec3(0, 1, 0),
        vec3(0, 1, 1),
        vec3(0, 0, 1),
        vec3(1, 1, 1),
        vec3(1, 0, 0),
        vec3(1, 1, 0),
        vec3(0, 1, 0),
        vec3(0, 1, 1)
    )[depthSlice];
    #endif

    #ifdef VIZ_CLUSTER_KEY
    /* Cluster key visualization */
    fragmentColor.rgb = clusterKey/vec3(TILES_X, TILES_Y, DEPTH_SLICES);
    #endif

    #ifdef ALPHA_MASK
    /* Using <= because if mask is set to 1.0, it should discard all, similarly
       as when using 0, it should only discard what's already invisible
       anyway. */
    if(fragmentColor.a <= alphaMask) discard;
    #endif

    #ifdef OBJECT_ID
    fragmentObjectId = objectId;
    #endif
}
