layout (location = 0) in vec4 position;

uniform mat4 transformationMatrix;
uniform mat4 projectionMatrix;

out vec4 fragPos;

void main() {
    gl_Position =
        projectionMatrix*
        transformationMatrix*
        vec4(position.xyz, 1.0f);
}
