#version 330 core
in vec3 ObjPos;
in vec3 CamObj;
out vec4 FragColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 LightPosWorld;

// The six baked planes, in layers X+, X-, Y+, Y-, Z+, Z- (layer = axis * 2 + (negative ? 1 : 0)).
// Each texel is R = U, G = V (into Color_Map), B = depth from the plane as a fraction of the
// box thickness along its axis, A = silhouette mask. Texel (s, t) spans the plane's local
// coordinates ((axis + 1) % 3, (axis + 2) % 3) over the box extents.
uniform sampler2DArray Plane_Maps;
uniform sampler2D Color_Map;

uniform vec3 BoxMin;
uniform vec3 BoxMax;

uniform bool ReliefEnabled;
uniform int LinearSteps;
uniform int DebugView;  // 0 Shaded, 1 Steps, 2 Hit plane, 3 UV, 4 Flat color (wireframe overlay)

#define MASK_THRESHOLD 0.5
#define DEPTH_EPS 2e-3
#define REFINE_STEPS 10

/**
 * Samples plane `layer` at the projection of `q` (position in box-normalized [0,1]^3 space).
 */
vec4 samplePlane(int layer, vec3 q) {
    int axis = layer / 2;
    vec2 st = vec2(q[(axis + 1) % 3], q[(axis + 2) % 3]);
    return textureLod(Plane_Maps, vec3(st, float(layer)), 0.0);
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
        vec4 p = samplePlane(axis * 2, q);
        vec4 m = samplePlane(axis * 2 + 1, q);
        if (p.a < MASK_THRESHOLD || m.a < MASK_THRESHOLD)
            return false;
        if (planeDepth(axis * 2, q) < p.b || planeDepth(axis * 2 + 1, q) < m.b)
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
        vec4 s = samplePlane(layer, q);
        if (s.a < MASK_THRESHOLD)
            continue;
        float err = abs(planeDepth(layer, q) - s.b);
        if (err < bestErr) {
            bestErr = err;
            best = layer;
        }
    }
    return best;
}

/**
 * Surface normal (object space, pointing outward) from the depth gradient of plane `layer`
 * around `q`. Neighbouring texels outside the silhouette fall back to the center depth.
 */
vec3 planeNormal(int layer, vec3 q) {
    int axis = layer / 2;
    int u = (axis + 1) % 3;
    int v = (axis + 2) % 3;
    float sgn = (layer % 2 == 0) ? 1.0 : -1.0;
    vec3 ext = BoxMax - BoxMin;

    vec2 texel = 1.0 / vec2(textureSize(Plane_Maps, 0).xy);
    vec2 st = vec2(q[u], q[v]);
    vec4 c = textureLod(Plane_Maps, vec3(st, float(layer)), 0.0);

    float d[4];
    vec2 offs[4] = vec2[4](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1));
    for (int i = 0; i < 4; i++) {
        vec4 n = textureLod(Plane_Maps, vec3(st + offs[i] * texel, float(layer)), 0.0);
        d[i] = n.a >= MASK_THRESHOLD ? n.b : c.b;
    }

    // Distance (in object units) of the surface from the plane, per unit of s and t.
    float dds = (d[1] - d[0]) * ext[axis] / (2.0 * texel.x * ext[u]);
    float ddt = (d[3] - d[2]) * ext[axis] / (2.0 * texel.y * ext[v]);

    // The surface is A(s, t) = c - sgn * depth along `axis`, so its outward normal is
    // proportional to (dds, ddt) in (u, v) and sgn along `axis`.
    vec3 n = vec3(0.0);
    n[u] = dds;
    n[v] = ddt;
    n[axis] = sgn;
    return normalize(n);
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
    bool hit = false;
    int stepsTaken = 0;

    if (ReliefEnabled) {
        float dt = (tEnd - tStart) / float(steps);
        float prevT = tStart;
        for (int i = 0; i <= steps; i++) {
            stepsTaken = i + 1;
            float t = tStart + dt * float(i);
            vec3 q = (CamObj + dir * t - BoxMin) / extent;
            if (insideSolid(q)) {
                // Binary search between the last outside sample and this one.
                float lo = prevT, hi = t;
                if (i == 0)
                    lo = hi;
                for (int k = 0; k < REFINE_STEPS && lo < hi; k++) {
                    float mid = 0.5 * (lo + hi);
                    vec3 qm = (CamObj + dir * mid - BoxMin) / extent;
                    if (insideSolid(qm))
                        hi = mid;
                    else
                        lo = mid;
                }
                hitT = hi;
                hit = true;
                break;
            }
            prevT = t;
        }
        if (!hit)
            discard;
    } else {
        hit = true;
    }

    vec3 hitObj = CamObj + dir * hitT;
    vec3 q = clamp((hitObj - BoxMin) / extent, 0.0, 1.0);

    // Without relief this is the point where the ray enters the box. Either way, a point that
    // falls outside every mask (e.g. right at a silhouette edge) is not drawn.
    int layer = bestPlane(q);
    if (layer < 0)
        discard;

    vec4 plane = samplePlane(layer, q);
    vec2 uv = plane.rg;

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
    if (DebugView == 3) {
        FragColor = vec4(uv, 0.0, 1.0);
        return;
    }

    vec3 albedo = textureLod(Color_Map, uv, 0.0).rgb;
    vec3 nObj = planeNormal(layer, q);
    vec3 nWorld = normalize(mat3(model) * nObj);

    vec3 lightDir = normalize(LightPosWorld - hitWorld.xyz);
    float diff = max(dot(nWorld, lightDir), 0.0);
    FragColor = vec4(albedo * (0.3 + 0.7 * diff), 1.0);
}
