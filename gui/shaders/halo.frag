#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec3 Color;

void main() {
    float d = length(vUV);
    // Soft radial falloff, squared for a tighter, less "disc-y" glow.
    float alpha = smoothstep(1.0, 0.0, d);
    alpha *= alpha;
    FragColor = vec4(Color, alpha);
}
