#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 tangent;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 viewPosWorld;

// Object space is the space of the (bounding box) mesh's own vertices.
out vec3 ObjPos;
out vec3 CamObj;

void main() {
    ObjPos = position;
    // The model matrix is a uniform scale plus a translation, so inverting it
    // per vertex is cheap and the result interpolates linearly.
    CamObj = vec3(inverse(model) * vec4(viewPosWorld, 1.0));
    gl_Position = projection * view * model * vec4(position, 1.0);
}
