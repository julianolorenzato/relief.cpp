#version 330 core
layout(location = 0) in vec3 position;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main() {
    FragPos = vec3(model * vec4(position, 1.0));
    // The sphere is unit-radius and centered at the origin in object space,
    // and `model` only ever applies a uniform scale + translation to it
    // (see ReliefView::paintGL), so the object-space position is already
    // the surface normal direction — no normal matrix needed.
    Normal = normalize(mat3(model) * position);
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
