uniform highp sampler2D depth;
uniform vec2 viewportScale;
uniform vec2 tileSize;
uniform float tanFov;
uniform mat4 inverseProjection;
uniform vec2 projectionParams;

out uint color;

void main() {
    vec2 pos = gl_FragCoord.xy;
    vec2 tileOffset = tileSize*pos;

    float near = projectionParams.x;
    float far = projectionParams.y;
    const float depthSlices = DEPTH_SLICES;

    color = 0u;
    float scale = depthSlices/log(far/near);
    float offset = depthSlices*log(near)/log(far/near);
    for(int x = 0; x < tileSize.x; ++x) {
        for(int y = 0; y < tileSize.y; ++y) {
            vec2 screenPos = (tileOffset + vec2(x, y));
            vec2 uv = viewportScale*screenPos;
            float d = texture2D(depth, uv).x;

            /* Formula to convert to depth slice index expects depth
             * in view space. We convert the depth from the depthbuffer
             * to view space by applying the inverse of the projection */

            float depth = near + d*(far - near);
            uint k = uint(log(depth)*scale - offset);
            color = color | (1u << k);
        }
    }

    color = color & 0xFFFFu;
}
