uniform vec2 scale;
uniform vec4 projectionParams;

out uint depthSlice;

uniform vec4 planes[4];

uint computeDepthSlice(float d) {
    return uint(log2(d)*projectionParams.z - projectionParams.w);
}

float linearDepth(float d) {
    float near = projectionParams.x;
    float far = projectionParams.y;

    return 2.0*near*far/(far + near - (2.0*d - 1.0)*(far - near));
}

void main() {
    /* Only assign depth values */

    vec4 d = gl_FragCoord.z;
    depthSlice = ;

    //uvec4 p0 = floatBitsToUint(d - planes[0]) >> vec4(32, 31, 30, 29);
    //uvec4 p1 = floatBitsToUint(d - planes[1]) >> vec4(28, 27, 26, 24);
    //uvec4 p2 = floatBitsToUint(d - planes[2]) >> vec4(24, 23, 22, 21);
    //uvec4 p3 = floatBitsToUint(d - planes[3]) >> vec4(20, 19, 18, 17);

    //depthSlice = computeDepthSlice();
}
