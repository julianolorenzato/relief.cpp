#version 330 core
in vec3 ObjPos;
in vec3 CamObj;
out vec4 FragColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 LightPosWorld;

// The baked views along the three axes X, Y, Z. Texel (s, t) spans the view's local coordinates
// ((axis + 1) % 3, (axis + 2) % 3) over the box extents.
// Depth_Maps (layer = axis): the surfaces a ray along the axis hits, R, G, B, A = hits 0..3 sorted
// from the axis' negative end, as a fraction of the box thickness rescaled by DEPTH_SCALE; 1.0
// marks a missing hit. A point is inside the solid when an odd number of hits lie before it.
uniform sampler2DArray Depth_Maps;
// Baked color of every hit (layer = axis * HITS + hit): RGB = albedo.
uniform sampler2DArray Color_Maps;
// Smooth object-space normals of every hit, octahedral-encoded: RG = encoded * 0.5 + 0.5.
uniform sampler2DArray Normal_Maps;

uniform vec3 BoxMin;
uniform vec3 BoxMax;

uniform bool ReliefEnabled;
uniform int LinearSteps;    // Number of uniform steps of the ray search
uniform int DebugView;  // 0 Shaded, 1 Steps, 2 Hit layer, 3 Normal, 4 Flat color (wireframe overlay)

#define HITS 4  // Mesh::kAxisHits
#define DEPTH_SCALE (65535.0 / 65534.0)  // Mesh::kDepthMax maps to 1.0 once rescaled
#define DEPTH_EPS 2e-3
#define DEPTH_TOLERANCE_TEXELS 4.0  // Depth difference, in texel widths of the box, still taken as the same surface
#define REFINE_STEPS 10

/**
 * Samples the hits of the ray along `axis` through `q` (position in box-normalized [0,1]^3
 * space), as fractions of the box thickness. A missing hit is above 1.0.
 */
vec4 sampleHits(int axis, vec3 q) {
    vec2 st = vec2(q[(axis + 1) % 3], q[(axis + 2) % 3]);
    vec4 d = textureLod(Depth_Maps, vec3(st, float(axis)), 0.0);
    return d * DEPTH_SCALE + step(vec4(1.0), d) * 2.0;
}

/**
 * Whether `q` lies inside the solid. Along each axis it is inside when an odd number of the ray's
 * hits lie before it (an entry without an exit leaves the solid open to the box end); the solid
 * is the intersection of the three axes.
 */
bool insideSolid(vec3 q) {
    for (int axis = 0; axis < 3; axis++) {
        vec4 before = vec4(lessThan(sampleHits(axis, q), vec4(q[axis])));
        if ((int(before.x + before.y + before.z + before.w) & 1) == 0)
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
 * Picks the layer that sees the surface point `q`: the hit, over the three axes, whose stored
 * depth best matches the point's own coordinate along that axis.
 * @return The layer (axis * HITS + hit), or -1 if no ray has a hit near the point.
 */
int bestLayer(vec3 q) {
    int best = -1;
    float bestErr = 1e9;
    for (int axis = 0; axis < 3; axis++) {
        vec4 hits = sampleHits(axis, q);
        for (int k = 0; k < HITS; k++) {
            if (hits[k] > 1.0)
                continue;
            float err = abs(q[axis] - hits[k]);
            if (err < bestErr) {
                bestErr = err;
                best = axis * HITS + k;
            }
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
 * Albedo and surface normal (object space, pointing outward) at `q`, read from the baked color and
 * smooth normal of `layer` (axis * HITS + hit).
 *
 * The four nearest texels are blended bilinearly, but only those whose stored hit lies at the depth
 * of `q`: next to a depth discontinuity (a fold, a silhouette) the neighbouring texel belongs to a
 * different surface, and blending it in leaves dark and bright lines along the contour. If no texel
 * qualifies, the nearest one is used as is.
 */
void sampleHit(int layer, vec3 q, out vec3 albedo, out vec3 normal) {
    int axis = layer / HITS;
    int hit = layer % HITS;
    ivec2 size = textureSize(Depth_Maps, 0).xy;
    vec2 st = vec2(q[(axis + 1) % 3], q[(axis + 2) % 3]) * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(st));
    vec2 f = st - vec2(base);
    float tol = DEPTH_TOLERANCE_TEXELS / float(size.x);

    vec3 c = vec3(0.0);
    vec3 n = vec3(0.0);
    float total = 0.0;
    for (int j = 0; j < 4; j++) {
        ivec2 o = ivec2(j & 1, j >> 1);
        ivec2 p = clamp(base + o, ivec2(0), size - 1);
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        float d = texelFetch(Depth_Maps, ivec3(p, axis), 0)[hit];
        if (w <= 0.0 || d >= 1.0 || abs(d * DEPTH_SCALE - q[axis]) > tol)
            continue;
        c += w * texelFetch(Color_Maps, ivec3(p, layer), 0).rgb;
        n += w * octDecode(texelFetch(Normal_Maps, ivec3(p, layer), 0).rg * 2.0 - 1.0);
        total += w;
    }
    if (total > 0.0) {
        albedo = c / total;
        normal = normalize(n);
    } else {
        ivec2 p = clamp(ivec2(floor(st + 0.5)), ivec2(0), size - 1);
        albedo = texelFetch(Color_Maps, ivec3(p, layer), 0).rgb;
        normal = octDecode(texelFetch(Normal_Maps, ivec3(p, layer), 0).rg * 2.0 - 1.0);
    }
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
    int layer = bestLayer(q);
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
        // Axis picks the hue (X red, Y green, Z blue), the hit index darkens it.
        vec3 base = vec3(0.0);
        base[layer / HITS] = 1.0;
        FragColor = vec4(mix(base + 0.2, base * 0.25, float(layer % HITS) / float(HITS - 1)), 1.0);
        return;
    }

    vec3 albedo, nObj;
    sampleHit(layer, q, albedo, nObj);
    if (DebugView == 3) {
        FragColor = vec4(nObj * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 nWorld = normalize(mat3(model) * nObj);

    vec3 lightDir = normalize(LightPosWorld - hitWorld.xyz);
    float diff = max(dot(nWorld, lightDir), 0.0);
    FragColor = vec4(albedo * (0.3 + 0.7 * diff), 1.0);
}
