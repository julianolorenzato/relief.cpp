#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 tangent;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 viewPosWorld;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec3 Tangent;
out vec3 ViewDirTS;

void main() {
    FragPos = vec3(model * vec4(position, 1.0));
    mat3 normalMat = mat3(transpose(inverse(model)));
    vec3 N = normalize(normalMat * normal);
    vec3 T = normalize(normalMat * tangent);
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T);

    mat3 worldToTangent = transpose(mat3(T, B, N));
    vec3 viewDirWorld = normalize(viewPosWorld - FragPos);
    ViewDirTS = worldToTangent * viewDirWorld;

    Normal = N;
    Tangent = T;
    TexCoord = texCoord;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
