#version 330 core
in vec3 ObjPos;
in vec3 CamObj;
out vec4 FragColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 LightPosWorld;

// The six baked planes, in layers X+, X-, Y+, Y-, Z+, Z- (layer = axis * 2 + (negative ? 1 : 0)).
// Texel (s, t) spans the plane's local coordinates ((axis + 1) % 3, (axis + 2) % 3) over the box
// extents. Depth_Maps: R = depth from the plane as a fraction of the box thickness along its
// axis, rescaled by DEPTH_SCALE; 1.0 marks a texel the mesh does not cover (silhouette mask).
uniform sampler2DArray Depth_Maps;
// Baked color of the same planes: RGB = albedo.
uniform sampler2DArray Color_Maps;
// Smooth object-space normals of the same planes, octahedral-encoded: RG = encoded * 0.5 + 0.5.
uniform sampler2DArray Normal_Maps;

uniform vec3 BoxMin;
uniform vec3 BoxMax;

uniform bool ReliefEnabled;
uniform int LinearSteps;    // Number of uniform steps of the ray search
uniform int DebugView;  // 0 Shaded, 1 Steps, 2 Hit plane, 3 Normal, 4 Flat color (wireframe overlay)

#define DEPTH_SCALE (65535.0 / 65534.0)  // Mesh::kDepthMax maps to 1.0 once rescaled
#define DEPTH_EPS 2e-3
#define REFINE_STEPS 10

/**
 * Samples the depth of plane `layer` at the projection of `q` (position in box-normalized
 * [0,1]^3 space), as a fraction of the box thickness. 2.0 where the mesh does not cover it.
 */
float sampleDepth(int layer, vec3 q) {
    int axis = layer / 2;
    vec2 st = vec2(q[(axis + 1) % 3], q[(axis + 2) % 3]);
    float d = textureLod(Depth_Maps, vec3(st, float(layer)), 0.0).r;
    return d >= 1.0 ? 2.0 : d * DEPTH_SCALE;
}

/**
 * Distance of `q` from plane `layer`, as a fraction of the box thickness along its axis.
 */
float planeDepth(int layer, vec3 q) {
    int axis = layer / 2;
    return (layer % 2 == 0) ? 1.0 - q[axis] : q[axis];
}

/**
 * Whether `q` lies inside the solid. Along each axis the solid spans from the nearest
 * surface seen from the + plane to the nearest one seen from the - plane, where both planes
 * cover the texel; the solid is the intersection of the three spans.
 */
bool insideSolid(vec3 q) {
    for (int axis = 0; axis < 3; axis++) {
        float p = sampleDepth(axis * 2, q);
        float m = sampleDepth(axis * 2 + 1, q);
        if (p > 1.0 || m > 1.0)
            return false;
        if (planeDepth(axis * 2, q) < p || planeDepth(axis * 2 + 1, q) < m)
            return false;
    }
    return true;
}

/**
 * Ray / box slab test in object space.
 * @return (tNear, tFar); tFar < tNear means a miss.
 */
vec2 intersectBox(vec3 origin, vec3 dir) {
    vec3 inv = 1.0 / dir;
    vec3 t0 = (BoxMin - origin) * inv;
    vec3 t1 = (BoxMax - origin) * inv;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    return vec2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

/**
 * Picks the plane that sees the surface point `q`: the covered plane whose stored depth best
 * matches the point's own depth from that plane.
 * @return The layer, or -1 if no plane covers the point.
 */
int bestPlane(vec3 q) {
    int best = -1;
    float bestErr = 1e9;
    for (int layer = 0; layer < 6; layer++) {
        float d = sampleDepth(layer, q);
        if (d > 1.0)
            continue;
        float err = abs(planeDepth(layer, q) - d);
        if (err < bestErr) {
            bestErr = err;
            best = layer;
        }
    }
    return best;
}

/**
 * Decodes an octahedral-encoded unit vector (the inverse of Mesh::bake's octEncode).
 */
vec3 octDecode(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

/**
 * Surface normal (object space, pointing outward) at `q`, read from the baked smooth normals of
 * plane `layer`.
 */
vec3 planeNormal(int layer, vec3 q) {
    int axis = layer / 2;
    vec2 e = texture(Normal_Maps, vec3(q[(axis + 1) % 3], q[(axis + 2) % 3], float(layer))).rg;
    return octDecode(e * 2.0 - 1.0);
}

/**
 * Albedo at `q`, read from the baked color of plane `layer`.
 */
vec3 planeColor(int layer, vec3 q) {
    int axis = layer / 2;
    return texture(Color_Maps, vec3(q[(axis + 1) % 3], q[(axis + 2) % 3], float(layer))).rgb;
}

/**
 * Uniform-step search followed by a binary refinement.
 *
 * @param[out] hitT  Ray parameter of the hit.
 * @param[out] iters Steps used.
 * @return Whether the ray hits the solid inside the box.
 */
bool marchLinear(vec3 q0, vec3 dq, float tStart, float tEnd, int steps, out float hitT, out int iters) {
    float dt = (tEnd - tStart) / float(steps);
    float prevT = tStart;
    iters = 0;
    hitT = tStart;
    for (int i = 0; i <= steps; i++) {
        iters = i + 1;
        float t = tStart + dt * float(i);
        if (insideSolid(q0 + dq * t)) {
            float lo = (i == 0) ? t : prevT;
            float hi = t;
            for (int k = 0; k < REFINE_STEPS && lo < hi; k++) {
                float mid = 0.5 * (lo + hi);
                if (insideSolid(q0 + dq * mid))
                    hi = mid;
                else
                    lo = mid;
            }
            hitT = hi;
            return true;
        }
        prevT = t;
    }
    return false;
}

void main() {
    if (DebugView == 4) {
        FragColor = vec4(1.0, 0.9, 0.3, 1.0);
        return;
    }

    vec3 dir = normalize(ObjPos - CamObj);
    vec2 tt = intersectBox(CamObj, dir);
    if (tt.y < max(tt.x, 0.0))
        discard;

    float tStart = max(tt.x, 0.0);
    float tEnd = tt.y;
    vec3 extent = max(BoxMax - BoxMin, vec3(1e-9));
    int steps = max(LinearSteps, 1);

    float hitT = tStart;
    int stepsTaken = 0;

    if (ReliefEnabled) {
        vec3 q0 = (CamObj - BoxMin) / extent;
        vec3 dq = dir / extent;
        bool hit = marchLinear(q0, dq, tStart, tEnd, steps, hitT, stepsTaken);
        if (!hit)
            discard;
    }

    vec3 hitObj = CamObj + dir * hitT;
    vec3 q = clamp((hitObj - BoxMin) / extent, 0.0, 1.0);

    // Without relief this is the point where the ray enters the box. Either way, a point that
    // falls outside every mask (e.g. right at a silhouette edge) is not drawn.
    int layer = bestPlane(q);
    if (layer < 0)
        discard;

    vec4 hitWorld = model * vec4(hitObj, 1.0);
    vec4 clip = projection * view * hitWorld;
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);

    if (DebugView == 1) {
        FragColor = vec4(vec3(float(stepsTaken) / float(steps + 1)), 1.0);
        return;
    }
    if (DebugView == 2) {
        const vec3 colors[6] = vec3[6](vec3(1, 0.2, 0.2), vec3(0.5, 0.1, 0.1), vec3(0.2, 1, 0.2),
                                       vec3(0.1, 0.5, 0.1), vec3(0.2, 0.4, 1), vec3(0.1, 0.2, 0.5));
        FragColor = vec4(colors[layer], 1.0);
        return;
    }

    vec3 nObj = planeNormal(layer, q);
    if (DebugView == 3) {
        FragColor = vec4(nObj * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 albedo = planeColor(layer, q);
    vec3 nWorld = normalize(mat3(model) * nObj);

    vec3 lightDir = normalize(LightPosWorld - hitWorld.xyz);
    float diff = max(dot(nWorld, lightDir), 0.0);
    FragColor = vec4(albedo * (0.3 + 0.7 * diff), 1.0);
}
