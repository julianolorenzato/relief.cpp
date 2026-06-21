#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec3 Tangent;
in vec3 ViewDirTS;
out vec4 FragColor;

uniform sampler2D Color_Map;
uniform sampler2D Relief_Map;
uniform sampler2D Relief_Map_Linear;
uniform sampler2D Offset_Map;
uniform sampler2D Normal_Map;

uniform int   ReliefType;               // 0 Off, 1 Linear, 3 Mip
uniform int   OffsetMapSamplingVersion;  // 1, 2, 3
uniform bool  UseAtlas;
uniform bool  Filter0;
uniform int   LinearSteps;
uniform int   BinarySteps;
uniform float DepthScale;
uniform float LastMip;
uniform float TexelSize;
uniform int   DebugView;                 // 0 Shaded, 1 Steps, 2 Leaps, 3 UV

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

vec4 SampleDepthMip(vec2 UV, float mip_level) {
    if (Filter0 && mip_level == 0.0)
        return textureLod(Relief_Map_Linear, UV, mip_level);
    return textureLod(Relief_Map, UV, mip_level);
}

// Equivalent to HLSL's mul(vec, RotationMatrix) with that function's row-major
// rotation literal (verified by hand-expanding the row-vector multiply).
vec2 rotateXY(vec2 v, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec2(v.x * c + v.y * s, -v.x * s + v.y * c);
}

void PerformIslandLeap(
    vec4 OffsetData,
    inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
    inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped, inout int numLeaps) {
    float RotationAngle = OffsetData.z * 6.28319;
    leaping_point = Current_Position.xy;
    tangent_direction.xy = rotateXY(tangent_direction.xy, RotationAngle);
    Current_Position.xy += OffsetData.xy;
    invDir = 1.0 / tangent_direction;
    landing_point = Current_Position.xy;
    jumped = true;
    numLeaps += 1;
}

void Island_leap_V1(
    inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
    inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped,
    inout int OffsetMapTextureSamples, inout int numLeaps) {
    vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
    OffsetMapTextureSamples += 1;
    if (OffsetData.w > 0.0)
        PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
    else
        jumped = false;
}

void Island_leap_V2(
    vec4 DepthData,
    inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
    inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped,
    inout int OffsetMapTextureSamples, inout int numLeaps) {
    if (abs(DepthData.w) > 0.0) {
        vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
        OffsetMapTextureSamples += 1;
        if (OffsetData.w > 0.0)
            PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
        else
            jumped = false;
    }
}

void Island_leap_V3(
    vec4 DepthData,
    inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
    inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped,
    inout int OffsetMapTextureSamples, inout int DiscriminantDepthTextureSamples, inout int numLeaps) {
    if (abs(DepthData.w) > 0.0) {
        vec4 DepthDiscriminant = textureLod(Relief_Map, Current_Position.xy, 0.0);
        DiscriminantDepthTextureSamples += 1;
        if (DepthDiscriminant.w != 0.0) {
            vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
            OffsetMapTextureSamples += 1;
            PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
        } else {
            jumped = false;
        }
    }
}

// GLSL port of relief.ush's CleanerRelief, with the relief map's mip-bound
// (G channel) used as the conservative per-cell depth and the project's
// UV-atlas island-leap hooks layered on top (neither of which relief.ush
// itself models: it works on a single, seamless depth channel).
vec3 Mip_Relief(
    vec3 Tangent_Direction, vec2 UV, int maxSteps, int OffsetMapSamplingVer,
    out int leapCounter, out int stepsOut) {
    float pixelSize = pow(2.0, -LastMip);
    float mip = 0.0;

    vec3 pos = vec3(UV, 0.0);
    vec3 invDir = 1.0 / Tangent_Direction;
    vec2 leapingPoint = vec2(0.0), landingPoint = vec2(0.0);
    bool jumped = false;

    int wallCount = 0, numLeaps = 0, offsetSamples = 0, discSamples = 0;
    int stepsTaken = 0;

    for (int i = 0; i < maxSteps; i++) {
        stepsTaken = i + 1;
        vec4 Depth = -SampleDepthMip(pos.xy, mip);

        if (UseAtlas) {
            if (OffsetMapSamplingVer == 1)
                Island_leap_V1(Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
            else if (OffsetMapSamplingVer == 2)
                Island_leap_V2(Depth, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
            else if (OffsetMapSamplingVer == 3)
                Island_leap_V3(Depth, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, discSamples, numLeaps);
        }

        vec3 aabbMin, aabbMax;
        BuildPixelAABB(pos.xy, Depth.y, pixelSize, aabbMin, aabbMax);

        // Not so deep, need to advance the ray
        if (pos.z > Depth.y + EPS) {
            bool bottomHit;
            pos = IntersectTheBox(pos, Tangent_Direction, invDir, aabbMin, aabbMax, bottomHit);

            if (!bottomHit) {
                wallCount++;
                if (wallCount >= 3) {
                    mip += 1.0;
                    pixelSize *= 2.0;
                }
                continue;
            }
        }

        // Mip-down (reached by: depth pass OR bottom hit). Depth.x == Depth.y
        // (avg == max-bound) is only ever true at mip 0, so it doubles as the
        // "can't refine any further" check.
        if (mip <= 0.0 || Depth.x == Depth.y) break;
        mip -= 1.0;
        pixelSize *= 0.5;
        wallCount = 0;
    }

    leapCounter = numLeaps;
    stepsOut = stepsTaken;
    return pos;
}

vec3 Linear_Relief(
    vec3 Tangent_Direction, vec2 UV, int maxSteps, int OffsetMapSamplingVer,
    out int leapCounter, out int stepsOut) {
    float sigma = 1.0 / float(maxSteps);
    vec3 pos = vec3(UV, 0.0);
    vec3 invDir = 1.0 / Tangent_Direction;
    vec2 leapingPoint = vec2(0.0), landingPoint = vec2(0.0);
    bool jumped = false;
    int numLeaps = 0, offsetSamples = 0, discSamples = 0;
    int stepsTaken = 0;

    for (int i = 0; i < maxSteps; i++) {
        stepsTaken = i + 1;
        vec4 DepthData = -textureLod(Relief_Map, pos.xy, 0.0);

        if (UseAtlas) {
            if (OffsetMapSamplingVer == 1)
                Island_leap_V1(Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
            else if (OffsetMapSamplingVer == 2)
                Island_leap_V2(DepthData, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
            else if (OffsetMapSamplingVer == 3)
                Island_leap_V3(DepthData, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, discSamples, numLeaps);
        }

        if (pos.z <= DepthData.x) break;
        pos.xy += sigma * Tangent_Direction.xy;
        pos.z  += sigma * Tangent_Direction.z;
    }

    leapCounter = numLeaps;
    stepsOut = stepsTaken;
    return pos;
}

vec2 BinarySearch(vec3 Starting_Point, vec3 Tangent_Direction, int Binary_Search_Steps) {
    vec3 Current_Position = Starting_Point;
    vec3 New_Tangent_Direction = Tangent_Direction;
    for (int i = 0; i < Binary_Search_Steps; i++) {
        float height = -textureLod(Relief_Map, Current_Position.xy, 0.0).x;
        Current_Position = (Current_Position.z > height)
            ? Current_Position + New_Tangent_Direction
            : Current_Position - New_Tangent_Direction;
        New_Tangent_Direction *= 0.5;
    }
    return Current_Position.xy;
}

vec2 ReliefMapping(vec3 Tangent_Direction, vec2 UV, out int leapCounter, out int stepsOut) {
    vec3 Tangent = Tangent_Direction;
    vec3 StartingPoint = vec3(UV, 0.0);
    int numLeaps = 0, numSteps = 0;

    if (ReliefType == 1) {
        StartingPoint = Linear_Relief(Tangent_Direction, UV, LinearSteps, OffsetMapSamplingVersion, numLeaps, numSteps);
    } else if (ReliefType == 3) {
        StartingPoint = Mip_Relief(Tangent_Direction, UV, LinearSteps, OffsetMapSamplingVersion, numLeaps, numSteps);
    }

    Tangent = Tangent * TexelSize;

    vec2 FinalUV = (ReliefType > 0) ? BinarySearch(StartingPoint, Tangent, BinarySteps) : UV;

    leapCounter = numLeaps;
    stepsOut = numSteps;
    return FinalUV;
}

void main() {
    vec3 N = normalize(Normal);
    vec3 T = normalize(Tangent - N * dot(N, Tangent));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    vec2 finalUV = TexCoord;
    int leapCounter = 0, stepsTaken = 0;

    if (ReliefType != 0) {
        vec3 viewTS = normalize(ViewDirTS);
        // Tangent-space ray marching into the surface: xy follows the view
        // direction's projection (silhouette-correct parallax), z is negative
        // because depth values are negative (z=0 is the untouched surface
        // plane, more negative is deeper) — see relief.ush's `depth` sign.
        vec3 tangentDir = vec3(-viewTS.xy / max(abs(viewTS.z), 1e-4) * DepthScale, -1.0);
        finalUV = ReliefMapping(tangentDir, TexCoord, leapCounter, stepsTaken);
    }

    if (DebugView == 1) { FragColor = vec4(vec3(float(stepsTaken) / float(max(LinearSteps, 1))), 1.0); return; }
    if (DebugView == 2) { FragColor = vec4(vec3(float(leapCounter) / 4.0), 1.0); return; }
    if (DebugView == 3) { FragColor = vec4(finalUV, 0.0, 1.0); return; }

    vec3 albedo = texture(Color_Map, finalUV).rgb;
    vec3 nTS = texture(Normal_Map, finalUV).rgb;
    vec3 shadingNormal = normalize(TBN * nTS);

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(shadingNormal, lightDir), 0.0);
    vec3 result = albedo * (0.3 + 0.7 * diff);
    FragColor = vec4(result, 1.0);
}
