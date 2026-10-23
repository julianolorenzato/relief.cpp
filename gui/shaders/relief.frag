#version 330 core
in vec3 ObjPos;
in vec3 CamObj;
out vec4 FragColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 LightPosWorld;

// The six baked planes, in layers X+, X-, Y+, Y-, Z+, Z- (layer = axis * 2 + (negative ? 1 : 0)).
// At mip 0 each texel is R = U, G = V (into Color_Map), B = depth from the plane as a fraction
// of the box thickness along its axis, A = silhouette mask. At coarser mips only B and A are
// used: B is the minimum depth over the covered texels of the cell, A is 1 if any is covered. Texel (s, t) spans the plane's local
// coordinates ((axis + 1) % 3, (axis + 2) % 3) over the box extents.
uniform sampler2DArray Plane_Maps;
// Smooth object-space normals of the same planes: RGB = normal * 0.5 + 0.5, A = mask.
uniform sampler2DArray Normal_Maps;
uniform sampler2D Color_Map;

uniform vec3 BoxMin;
uniform vec3 BoxMax;

uniform bool ReliefEnabled;
uniform bool UseMipSearch;  // true: mip-hierarchical search, false: uniform linear steps
uniform int PlaneMips;      // Mip levels of Plane_Maps
uniform int LinearSteps;    // Linear steps, or the iteration budget of the mip search
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
 * Surface normal (object space, pointing outward) at `q`, read from the baked smooth normals of
 * plane `layer`. Falls back to the plane's own direction where the normal map is not covered.
 */
vec3 planeNormal(int layer, vec3 q) {
    int axis = layer / 2;
    vec4 s = textureLod(Normal_Maps, vec3(q[(axis + 1) % 3], q[(axis + 2) % 3], float(layer)), 0.0);
    vec3 fallback = vec3(0.0);
    fallback[axis] = (layer % 2 == 0) ? 1.0 : -1.0;
    if (s.a < MASK_THRESHOLD)
        return fallback;
    vec3 n = s.rgb * 2.0 - 1.0;
    return dot(n, n) > 1e-6 ? normalize(n) : fallback;
}

/**
 * Largest t' >= t such that, judging by axis `axis` alone, the ray cannot be inside the solid
 * anywhere in [t, t'). Returns t itself when the ray is inside this axis' cell box, i.e. nothing
 * can be skipped.
 *
 * The cell at mip level `mip` under the ray is a box: its (u, v) footprint times the depth span
 * [min dM, 1 - min dP] that the solid can occupy under it (empty if the cell is uncovered).
 * Outside that box the solid is absent, so the ray can jump to where it enters the box, or, if
 * it misses the box, to where it leaves the cell's column.
 *
 * @param q0 Ray origin in box-normalized space.
 * @param dq Ray direction in box-normalized space.
 */
float axisAdvance(int axis, vec3 q0, vec3 dq, float t, int mip) {
    int u = (axis + 1) % 3;
    int v = (axis + 2) % 3;
    vec3 q = q0 + dq * t;

    ivec2 size = textureSize(Plane_Maps, mip).xy;
    vec2 n = vec2(size);
    ivec2 cell = clamp(ivec2(floor(vec2(q[u], q[v]) * n)), ivec2(0), size - 1);
    vec2 cmin = vec2(cell) / n;
    vec2 cmax = vec2(cell + 1) / n;

    // Exit of the cell's column.
    vec2 d = vec2(dq[u], dq[v]);
    vec2 qq = vec2(q[u], q[v]);
    float tCol = 1e30;
    for (int i = 0; i < 2; i++) {
        if (d[i] > 1e-12)
            tCol = min(tCol, (cmax[i] - qq[i]) / d[i]);
        else if (d[i] < -1e-12)
            tCol = min(tCol, (cmin[i] - qq[i]) / d[i]);
    }
    tCol += t;

    vec4 p = texelFetch(Plane_Maps, ivec3(cell, axis * 2), mip);
    vec4 m = texelFetch(Plane_Maps, ivec3(cell, axis * 2 + 1), mip);
    float lo = m.b;
    float hi = 1.0 - p.b;
    if (p.a < MASK_THRESHOLD || m.a < MASK_THRESHOLD || lo > hi)
        return tCol;

    // Parameter range where the ray is within [lo, hi] along `axis`.
    float tIn, tOut;
    if (abs(dq[axis]) < 1e-12) {
        if (q[axis] < lo || q[axis] > hi)
            return tCol;
        tIn = -1e30;
        tOut = 1e30;
    } else {
        float t1 = t + (lo - q[axis]) / dq[axis];
        float t2 = t + (hi - q[axis]) / dq[axis];
        tIn = min(t1, t2);
        tOut = max(t1, t2);
    }
    tOut = min(tOut, tCol);

    if (tOut < t || tIn > tOut)
        return tCol;
    return max(tIn, t);
}

/**
 * Mip-hierarchical search for the first point inside the solid. Each iteration takes the
 * largest skip allowed by any of the three axes; after a few consecutive skips it moves to a
 * coarser mip, and when nothing can be skipped it moves to a finer one. At mip 0 the cells
 * are single texels, so "nothing to skip on all three axes" is exactly insideSolid().
 *
 * @param[out] hitT  Ray parameter of the hit.
 * @param[out] iters Iterations used.
 * @return Whether the ray hits the solid inside the box.
 */
bool marchMip(vec3 q0, vec3 dq, float tStart, float tEnd, int budget, out float hitT, out int iters) {
    float epsT = 1e-5 * (tEnd - tStart);
    float t = tStart;
    int mip = 0;
    int skips = 0;
    iters = 0;
    hitT = t;

    for (int i = 0; i < budget; i++) {
        iters = i + 1;
        if (t > tEnd)
            return false;

        float next = t;
        for (int axis = 0; axis < 3; axis++)
            next = max(next, axisAdvance(axis, q0, dq, t, mip));

        if (next > t) {
            t = next + epsT;
            if (++skips >= 3 && mip < PlaneMips - 1) {
                mip++;
                skips = 0;
            }
            continue;
        }

        if (mip > 0) {
            mip--;
            skips = 0;
            continue;
        }
        hitT = t;
        return true;
    }
    return false;
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
        bool hit = UseMipSearch ? marchMip(q0, dq, tStart, tEnd, steps, hitT, stepsTaken)
                                : marchLinear(q0, dq, tStart, tEnd, steps, hitT, stepsTaken);
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
