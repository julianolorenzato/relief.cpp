#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec3 Tangent;
in float Handedness;
in vec3 ViewDirTS;
out vec4 FragColor;

uniform sampler2D Color_Map;
uniform sampler2D Relief_Map;
uniform sampler2D Offset_Map;
uniform sampler2D Normal_Map;

uniform bool ReliefEnabled;
uniform bool UseAtlas;
uniform int ReliefTextureType;   // 0 = depth map (white=deep), 1 = height map (white=high)
uniform int LinearSteps;
uniform float DepthScale;
uniform float LastMip;
uniform int DebugView;                 // 0 Shaded, 1 Steps, 2 Leaps, 3 UV

// Matches relief.ush's `eps` — also used as the AABB epsilon and the
// box-intersection threshold below.
#define EPS 1e-3

// GLSL port of relief.ush's IntersectTheBox. Reports whether the AABB exit
// was through the bottom (depth) face rather than an XY wall, so the caller
// can tell a "reached the surface" event apart from a "crossed into the next
// texel" event.
vec3 IntersectTheBox(vec3 rayOrigin, vec3 rayDir, vec3 invRayDir, vec3 aabbMin, vec3 aabbMax, out bool bottomHit) {
    vec3 t0 = (aabbMin - rayOrigin) * invRayDir;
    vec3 t1 = (aabbMax - rayOrigin) * invRayDir;
    vec3 tSlabExit = max(t0, t1);

    float tFar = min(tSlabExit.x, min(tSlabExit.y, tSlabExit.z));
    bottomHit = (tSlabExit.z <= tSlabExit.x) && (tSlabExit.z <= tSlabExit.y);

    return clamp(rayOrigin + tFar * rayDir, aabbMin, aabbMax);
}

// GLSL port of relief.ush's BuildPixelAABB.
void BuildPixelAABB(vec2 uv, float depth, float pixelSize, out vec3 aabbMin, out vec3 aabbMax) {
    vec2 pixelCoord = floor(uv / pixelSize) * pixelSize;
    aabbMin = vec3(pixelCoord - EPS, depth);
    aabbMax = vec3(pixelCoord + pixelSize + EPS, 0.0);
}

// Equivalent to HLSL's mul(vec, RotationMatrix) with that function's row-major
// rotation literal (verified by hand-expanding the row-vector multiply).
vec2 rotateXY(vec2 v, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec2(v.x * c + v.y * s, -v.x * s + v.y * c);
}

void PerformIslandLeap(
    vec4 OffsetData,
    inout vec3 tangent_direction,
    inout vec3 Current_Position,
    inout vec3 invDir,
    inout vec2 leaping_point,
    inout vec2 landing_point,
    inout bool jumped,
    inout int numLeaps
) {
    float RotationAngle = OffsetData.z * 6.28319;
    leaping_point = Current_Position.xy;
    tangent_direction.xy = rotateXY(tangent_direction.xy, RotationAngle);
    Current_Position.xy += OffsetData.xy;
    invDir = 1.0 / tangent_direction;
    landing_point = Current_Position.xy;
    jumped = true;
    numLeaps += 1;
}

// Skips the ray across a UV-atlas seam/edge: samples the Offset_Map at the
// current position and, if it carries a leap (Offset_Map.w > 0), rotates the
// tangent direction and relocates the ray to the island's landing point.
void IslandLeap(
    inout vec3 tangent_direction,
    inout vec3 Current_Position,
    inout vec3 invDir,
    inout vec2 leaping_point,
    inout vec2 landing_point,
    inout bool jumped,
    inout int OffsetMapTextureSamples,
    inout int numLeaps
) {
    vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
    OffsetMapTextureSamples += 1;
    if(OffsetData.w > 0.0)
        PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
    else
        jumped = false;
}

// GLSL port of relief.ush's CleanerRelief, with the project's UV-atlas
// island-leap hook layered on top (relief.ush itself doesn't model atlas
// seams: it works on a single, seamless depth channel). Only the relief
// map's R channel (min depth) is used as the per-cell depth bound for now —
// the G channel (max depth, pooled per mip region — see
// downsampleReliefMixed) is reserved for a future conservative-stepping
// optimization.
vec3 Mip_Relief(
    vec3 Tangent_Direction,
    vec2 UV,
    int maxSteps,
    out int leapCounter,
    out int stepsOut
) {
    float pixelSize = pow(2.0, -LastMip);
    float mip = 0.0;

    vec3 pos = vec3(UV, 0.0);
    vec3 invDir = 1.0 / Tangent_Direction;
    vec2 leapingPoint = vec2(0.0), landingPoint = vec2(0.0);
    bool jumped = false;

    int wallCount = 0, numLeaps = 0, offsetSamples = 0;
    int stepsTaken = 0;

    for(int i = 0; i < maxSteps; i++) {
        stepsTaken = i + 1;
        vec2 rm = textureLod(Relief_Map, pos.xy, mip).xy;
        float s = (ReliefTextureType == 1) ? rm.y : rm.x;
        float depth = (ReliefTextureType == 1 ? s - 1.0 : -s) * DepthScale;

        if(UseAtlas)
            IslandLeap(Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);

        vec3 aabbMin, aabbMax;
        BuildPixelAABB(pos.xy, depth, pixelSize, aabbMin, aabbMax);

        // Not so deep, need to advance the ray
        if(pos.z > depth + EPS) {
            bool bottomHit;
            pos = IntersectTheBox(pos, Tangent_Direction, invDir, aabbMin, aabbMax, bottomHit);

            if(!bottomHit) {
                wallCount++;
                if(wallCount >= 3) {
                    mip += 1.0;
                    pixelSize *= 2.0;
                }
                continue;
            }
        }

        // Mip-down (reached by: depth pass OR bottom hit)
        if(mip <= 0.0)
            break;
        mip -= 1.0;
        pixelSize *= 0.5;
        wallCount = 0;
    }

    leapCounter = numLeaps;
    stepsOut = stepsTaken;
    return pos;
}

vec2 ReliefMapping(vec3 Tangent_Direction, vec2 UV, out int leapCounter, out int stepsOut) {
    int numLeaps = 0, numSteps = 0;
    vec3 StartingPoint = Mip_Relief(Tangent_Direction, UV, LinearSteps, numLeaps, numSteps);

    leapCounter = numLeaps;
    stepsOut = numSteps;
    return StartingPoint.xy;
}

void main() {
    vec3 N = normalize(Normal);
    vec3 T = normalize(Tangent - N * dot(N, Tangent));
    vec3 B = cross(N, T) * Handedness;
    mat3 TBN = mat3(T, B, N);

    vec2 finalUV = TexCoord;
    int leapCounter = 0, stepsTaken = 0;

    if(ReliefEnabled) {
        vec3 viewTS = normalize(ViewDirTS);
        // Tangent-space ray marching into the surface: xy follows the view
        // direction's projection (silhouette-correct parallax), z is negative
        // because depth values are negative (z=0 is the untouched surface
        // plane, more negative is deeper) — see relief.ush's `depth` sign.
        // DepthScale scales the sampled height/depth values themselves (see
        // Mip_Relief), not this ray's slope — matching the UE material, and
        // keeping the mip-marching step dynamics independent of the depth slider.
        vec3 tangentDir = vec3(-viewTS.xy / max(abs(viewTS.z), 1e-4), -1.0);
        finalUV = ReliefMapping(tangentDir, TexCoord, leapCounter, stepsTaken);
    }

    if(DebugView == 1) {
        FragColor = vec4(vec3(float(stepsTaken) / float(max(LinearSteps, 1))), 1.0);
        return;
    }
    if(DebugView == 2) {
        FragColor = vec4(vec3(float(leapCounter) / 4.0), 1.0);
        return;
    }
    if(DebugView == 3) {
        FragColor = vec4(finalUV, 0.0, 1.0);
        return;
    }

    vec3 albedo = texture(Color_Map, finalUV).rgb;
    vec3 nTS = texture(Normal_Map, finalUV).rgb;
    vec3 shadingNormal = normalize(TBN * nTS);

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(shadingNormal, lightDir), 0.0);
    vec3 result = albedo * (0.3 + 0.7 * diff);
    FragColor = vec4(result, 1.0);
}
