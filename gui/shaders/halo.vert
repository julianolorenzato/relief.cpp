#version 330 core
// Quad corners in [-1, 1]; the camera-facing billboard transform is baked
// into `model` on the CPU side (see ReliefView::paintGL).
layout(location = 0) in vec2 quadPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec2 vUV;

void main() {
    vUV = quadPos;
    gl_Position = projection * view * model * vec4(quadPos, 0.0, 1.0);
}
