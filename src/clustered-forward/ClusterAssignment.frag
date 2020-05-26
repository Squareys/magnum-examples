uniform highp sampler2D depth;
uniform vec2 viewportScale;
uniform vec2 tileSize;
uniform float tanFov;
uniform mat4 inverseProjection;
uniform vec2 projectionParams;

out uint color;

float linearDepth(float d) {
    float near = projectionParams.x;
    float far = projectionParams.y;

    float depth = 2.0*d - 1.0;
    //return 2.0*near*far/(far + near - (2.0*depth - 1.0)*(far - near));
    return 2.0*near*far/(far + near - depth * (far - near));
}

uint depthSlice(float d) {
    float near = projectionParams.x;
    float far = projectionParams.y;
    const float depthSlices = DEPTH_SLICES;

    float lfn = log2(far/near);
    float scale = depthSlices/lfn;
    float offset = depthSlices*log2(near)/lfn;
    return uint(log2(d)*scale - offset);
}

void main() {
    vec2 pos = gl_FragCoord.xy - vec2(0.5);
    vec2 tileOffset = tileSize*pos;

    const float depthSlices = DEPTH_SLICES;
    float near = projectionParams.x;
    float far = projectionParams.y;

    color = 0u;
    for(int x = 0; x < tileSize.x; ++x) {
        for(int y = 0; y < tileSize.y; ++y) {
            vec2 screenPos = tileOffset + vec2(x, y);
            vec2 uv = viewportScale*screenPos;
            float d = texture(depth, uv).x;

            uint k = depthSlice(linearDepth(d));
            color = color | (1u << k);
        }
    }
    /* Remove out of bounds cells */
    color = color & 0xFFFFu;
}
