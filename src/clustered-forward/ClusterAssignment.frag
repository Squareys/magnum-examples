uniform highp sampler2D depth;
uniform vec2 viewportScale;
uniform vec2 tileSize;
uniform float tanFov;
uniform mat4 inverseProjection;
uniform vec4 projectionParams;

out uint color;

float linearDepth(float d) {
    float near = projectionParams.x;
    float far = projectionParams.y;

    return 2.0*near*far/(far + near - (2.0*d - 1.0)*(far - near));
}

uint depthSlice(float d) {
    return uint(log2(d)*projectionParams.z - projectionParams.w);
}

void mainSingle() {
    vec2 pos = gl_FragCoord.xy - vec2(0.5);
    vec2 tileOffset = tileSize*pos;

    color = 0u;
    for(int x = 0; x <= tileSize.x; ++x) {
        for(int y = 0; y <= tileSize.y; ++y) {
            vec2 screenPos = tileOffset + vec2(x, y);
            vec2 uv = viewportScale*screenPos;
            float d = texture(depth, uv).x;

            uint k = depthSlice(linearDepth(d));
            color = color | (1u << k);
        }
    }
}

void main4() {
    vec2 pos = gl_FragCoord.xy - vec2(0.5);
    vec2 tileOffset = tileSize*pos;

    color = 0u;
    for(int x = 0; x <= tileSize.x; x += 2) {
        for(int y = 0; y <= tileSize.y; y += 2) {
            /* Manually unrolled texture access to run on
             * 2x2 texels */
            vec2 screenPos = tileOffset + vec2(x, y);
            vec2 uv = viewportScale*screenPos;

            float d = texture(depth, uv).x;
            uint k = depthSlice(linearDepth(d));

            float d2 = texture(depth, uv + vec2(0, viewportScale.y)).x;
            uint k2 = depthSlice(linearDepth(d2));

            float d3 = texture(depth, uv + vec2(viewportScale.x, 0)).x;
            uint k3 = depthSlice(linearDepth(d3));

            float d4 = texture(depth, uv + viewportScale).x;
            uint k4 = depthSlice(linearDepth(d4));

            color = color | (1u << k) | (1u << k2) | (1u << k3) | (1u << k4);
        }
    }
}

void main() {
    main4();
    /* Remove out of bounds cells */
    color = color & 0xFFFFu;
}
