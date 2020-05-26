layout (location = 0) in highp vec4 position;

uniform highp mat4 projectionMatrix;
uniform highp mat4 transformationMatrix;
uniform highp mat4 viewMatrix;

out vec4 fragPos;

void main() {
    gl_Position =
        projectionMatrix*
        viewMatrix*
        transformationMatrix*
        vec4(position.xyz, 1.0f);
}
