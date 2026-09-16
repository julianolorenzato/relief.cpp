#version 330 core
in vec3 FragPos;
in vec3 Normal;
out vec4 FragColor;

uniform vec3 Color;
uniform vec3 viewPosWorld;

void main() {
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPosWorld - FragPos);

    // Fresnel-driven two-tone gradient (hot white-yellow core facing the
    // camera, fading to a warm orange rim at grazing angles) so the glyph
    // reads as a glowing sphere rather than a flat disc.
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 2.0);
    vec3 core = vec3(1.0, 0.98, 0.85);
    vec3 rim  = vec3(1.0, 0.45, 0.12);
    vec3 shaded = mix(core, rim, fresnel);

    // Small fixed-direction specular sparkle so the orb doesn't look flat
    // even head-on, where the fresnel term alone goes to zero.
    vec3 sparkleDir = normalize(V + vec3(0.4, 0.6, 0.3));
    float sparkle = pow(max(dot(N, sparkleDir), 0.0), 24.0);

    FragColor = vec4(clamp(shaded * Color + sparkle * 0.6, 0.0, 1.0), 1.0);
}
