#ifndef CRAFT_WATER_GLSL
#define CRAFT_WATER_GLSL

// SleakCraft water surface, shared by water_shader.frag (Vulkan) and
// water_shader_gl.frag. Linear scene units throughout; each stem converts its
// snapshot and sky first and fogs the result. Hooks a stem defines before
// including this file: CraftWaterScene, CraftWaterSky, CraftWaterHitFog,
// CraftWaterSunVisibility, ProjectToScreen and Reconstruct, plus the
// scene-read names sceneDepth, uViewProj, uScreenSize, uSceneCamera and
// uSceneFlags.

#include "craft/lighting.glsl"
#include "sleak/atmosphere.glsl"

// ---- Water body (BSL defaults) ----
// Authored tint and gain; the albedo is (tint x gain^2)^2
#define CRAFT_WATER_TINT (vec3(64.0, 160.0, 255.0) / 255.0)
#define CRAFT_WATER_TINT_GAIN 0.35
// Veil opacity at normal incidence; Fresnel takes it to 1
#define CRAFT_WATER_ALPHA 0.70
// Bed light lost per metre of view ray inside the water
#define CRAFT_WATER_EXTINCTION 0.20
// Lit like foliage: wrap and forward scattering
#define CRAFT_WATER_SSS 1.0
#define CRAFT_WATER_NO_LIGHT 0.0
#define CRAFT_WATER_FULL_AO 1.0

// ---- Optics ----
#define CRAFT_WATER_PI 3.14159265
#define CRAFT_WATER_IOR 1.333
#define CRAFT_AIR_IOR 1.0
#define CRAFT_WATER_F0                                     \
    pow((CRAFT_WATER_IOR - CRAFT_AIR_IOR) /                \
            (CRAFT_WATER_IOR + CRAFT_AIR_IOR), 2.0)
#define CRAFT_WATER_FRESNEL_POWER 5.0
// Critical angle seen from below and its soft edge
#define CRAFT_WATER_TIR_COS \
    sqrt(1.0 - pow(CRAFT_AIR_IOR / CRAFT_WATER_IOR, 2.0))
#define CRAFT_WATER_TIR_SOFTEN 0.04
// Glint: BSL's near-mirror (smoothness 0.9) on a light disc, soft-clamped
#define CRAFT_WATER_ROUGHNESS 0.01
#define CRAFT_WATER_LIGHT_TAN_RADIUS 0.02
#define CRAFT_WATER_SPEC_SOFT_MAX 128.0
// Probe blur per unit of filtered wave slope
#define CRAFT_WATER_SKY_LOD_PER_SLOPE 10.0
#define CRAFT_WATER_TOP_FACE_MIN_Y 0.5
#define CRAFT_WATER_EPSILON 1e-4
#define CRAFT_WATER_HALF 0.5

// ---- Waves: BSL's cellular ripples, Worley fBm in two drifting layers ----
#define CRAFT_WAVE_BROAD_CELL 7.5           // m, Worley cell of the broad layer
#define CRAFT_WAVE_DETAIL_CELL 1.4          // m, cell of the detail layer
#define CRAFT_WAVE_DETAIL 0.25              // detail layer share (WATER_DETAIL)
#define CRAFT_WAVE_OCTAVES 2                // fBm octaves per layer
#define CRAFT_WAVE_LAYERS 2
#define CRAFT_WAVE_LAYERS_BELOW 1           // seen from below: broad only
#define CRAFT_WAVE_LACUNARITY 2.0
#define CRAFT_WAVE_GAIN 0.5
// Surface slope per unit of height gradient
#define CRAFT_WAVE_SLOPE_GAIN 0.5
// Soft minimum over the feature distances: larger creases harder
#define CRAFT_WAVE_PIT_SHARPNESS 8.0
#define CRAFT_WAVE_MIN_WEIGHT 1e-20
// Worley search: the cell holding p and its eight neighbours
#define CRAFT_WAVE_SEARCH_WIDTH 3
#define CRAFT_WAVE_SEARCH_OFFSET 1
#define CRAFT_WAVE_WIND 0.5                 // m/s on both axes, layers opposed
#define CRAFT_WAVE_OCTAVE_SHIFT vec2(37.2, 17.9)
#define CRAFT_WAVE_FADE_START 0.08          // pixel footprint / cell
#define CRAFT_WAVE_FADE_END 0.25
// Bump kept toward grazing views: 1 - (1 - |N.V|)^power
#define CRAFT_WAVE_GRAZING_POWER 8.0
// Far calm
#define CRAFT_WAVE_CALM_START 30.0
#define CRAFT_WAVE_CALM_END 100.0
#define CRAFT_WAVE_FAR_SLOPE_SCALE 0.35

// ---- Screen-space reflection ----
#define CRAFT_WATER_SSR_MIN_FRESNEL 0.04
#define CRAFT_WATER_SSR_STEPS 12
#define CRAFT_WATER_SSR_BATCH 4             // depth fetches issued together
#define CRAFT_WATER_SSR_REFINE_STEPS 4
#define CRAFT_WATER_SSR_START 1.0           // m
#define CRAFT_WATER_SSR_GROWTH 2.0
#define CRAFT_WATER_SSR_STEP_ADD 0.1        // m
#define CRAFT_WATER_SSR_MAX_DIST 256.0      // m
#define CRAFT_WATER_SSR_MIN_THICKNESS 0.75  // m
#define CRAFT_WATER_SSR_THICKNESS_STEPS 2.0
#define CRAFT_WATER_SSR_EDGE_FADE 0.07      // uv
#define CRAFT_WATER_SSR_DIST_FADE_START 0.6
#define CRAFT_WATER_SSR_ORIGIN_OFFSET 0.02  // m along the normal
#define CRAFT_WATER_SSR_MIN_DIR_Y 0.015

// ---- Integer lattice hash: per axis, then mixed per cell ----
#define CRAFT_WATER_HASH_AXIS_X 0x8da6b343u
#define CRAFT_WATER_HASH_AXIS_Y 0xd8163841u
#define CRAFT_WATER_HASH_MIX 0x5bd1e995u
#define CRAFT_WATER_HASH_SHIFT 15u
#define CRAFT_WATER_HASH_HALF_BITS 16u
#define CRAFT_WATER_HASH_LOW_MASK 0xffffu
#define CRAFT_WATER_HASH_UNIT (1.0 / 65536.0)

/// One water fragment: linear scene color before fog, and its alpha.
struct CraftWaterResult {
    vec3 color;
    float alpha;  // 1 once the scene was read, else the veil to blend
};

/// Lattice hash of one axis coordinate.
uint CraftWaterAxisHash(float v, uint axis) {
    uint h = uint(int(v)) * axis;
    return h ^ (h >> CRAFT_WATER_HASH_SHIFT);
}

/// Two values in [0, 1) from the hashes of a cell's axes.
vec2 CraftWaterCellHash(uint hx, uint hy) {
    uint h = (hx ^ hy) * CRAFT_WATER_HASH_MIX;
    h ^= h >> CRAFT_WATER_HASH_SHIFT;
    return vec2(uvec2(h & CRAFT_WATER_HASH_LOW_MASK,
                      h >> CRAFT_WATER_HASH_HALF_BITS)) *
           CRAFT_WATER_HASH_UNIT;
}

/// Gradient at p (cell units) of a soft minimum of the squared distances to
/// the feature points of a Worley grid: round pits with soft creases.
vec2 CraftWaterPitGradient(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = p - cell;
    uint hx[CRAFT_WAVE_SEARCH_WIDTH], hy[CRAFT_WAVE_SEARCH_WIDTH];
    for (int i = 0; i < CRAFT_WAVE_SEARCH_WIDTH; ++i) {
        float o = float(i - CRAFT_WAVE_SEARCH_OFFSET);
        hx[i] = CraftWaterAxisHash(cell.x + o, CRAFT_WATER_HASH_AXIS_X);
        hy[i] = CraftWaterAxisHash(cell.y + o, CRAFT_WATER_HASH_AXIS_Y);
    }
    float sum = 0.0;
    vec2 grad = vec2(0.0);
    for (int j = 0; j < CRAFT_WAVE_SEARCH_WIDTH; ++j) {
        for (int i = 0; i < CRAFT_WAVE_SEARCH_WIDTH; ++i) {
            vec2 o = vec2(float(i - CRAFT_WAVE_SEARCH_OFFSET),
                          float(j - CRAFT_WAVE_SEARCH_OFFSET));
            vec2 d = f - o - CraftWaterCellHash(hx[i], hy[j]);
            float w = exp(-CRAFT_WAVE_PIT_SHARPNESS * dot(d, d));
            sum += w;
            grad += w * d;
        }
    }
    return 2.0 * grad / max(sum, CRAFT_WAVE_MIN_WEIGHT);
}

/// Wave normal at world xz: xyz the normal, w the slope variance the
/// footprint filter and the far calm removed (it blurs the sky probe).
/// Seen from below, the broad layer only.
vec4 CraftWaveNormal(vec2 xz, float t, float footprint, float viewDist,
                     float facing, bool below) {
    float calm = mix(1.0, CRAFT_WAVE_FAR_SLOPE_SCALE,
                     smoothstep(CRAFT_WAVE_CALM_START, CRAFT_WAVE_CALM_END,
                                viewDist));
    vec2 wind = vec2(t * CRAFT_WAVE_WIND);
    // fBm weights sum to one; each octave keeps its layer's slope
    float norm = (1.0 - CRAFT_WAVE_GAIN) /
                 (1.0 - pow(CRAFT_WAVE_GAIN, float(CRAFT_WAVE_OCTAVES)));
    vec2 g = vec2(0.0);
    float lost = 0.0;
    int layers = below ? CRAFT_WAVE_LAYERS_BELOW : CRAFT_WAVE_LAYERS;
    for (int layer = 0; layer < layers; ++layer) {
        bool broad = layer == 0;
        float freq =
            1.0 / (broad ? CRAFT_WAVE_BROAD_CELL : CRAFT_WAVE_DETAIL_CELL);
        float share = broad ? 1.0 - CRAFT_WAVE_DETAIL : CRAFT_WAVE_DETAIL;
        float slope = CRAFT_WAVE_SLOPE_GAIN * share * norm * freq;
        vec2 p = (xz + (broad ? -wind : wind)) * freq;
        for (int o = 0; o < CRAFT_WAVE_OCTAVES; ++o) {
            float fade = (1.0 - smoothstep(CRAFT_WAVE_FADE_START,
                                           CRAFT_WAVE_FADE_END,
                                           footprint * freq)) *
                         calm;
            lost += slope * slope * (1.0 - fade * fade);
            if (fade > 0.0) g += CraftWaterPitGradient(p) * (slope * fade);
            p = p * CRAFT_WAVE_LACUNARITY + CRAFT_WAVE_OCTAVE_SHIFT;
            freq *= CRAFT_WAVE_LACUNARITY;
            slope *= CRAFT_WAVE_GAIN * CRAFT_WAVE_LACUNARITY;
        }
    }
    // BSL flattens the bump toward grazing views
    g *= 1.0 - pow(1.0 - clamp(facing, 0.0, 1.0), CRAFT_WAVE_GRAZING_POWER);
    return vec4(normalize(vec3(-g.x, 1.0, -g.y)), lost);
}

/// Schlick Fresnel of water at the cosine c.
float CraftWaterFresnel(float c) {
    return CRAFT_WATER_F0 +
           (1.0 - CRAFT_WATER_F0) *
               pow(clamp(1.0 - c, 0.0, 1.0), CRAFT_WATER_FRESNEL_POWER);
}

/// View-space distance of a stored scene depth.
float CraftWaterLinearDepth(float depth) {
    float n = uSceneCamera.z, f = uSceneCamera.w;
    return n * f /
           max(f - SleakDepthToNdc(depth) * (f - n), CRAFT_WATER_EPSILON);
}

/// True inside the screen.
bool CraftWaterOnScreen(vec2 uv) {
    return all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)));
}

/// Screen trace of a reflected ray: rgb the hit as the scene shows it
/// through fog, a its confidence (0 on a miss).
vec4 CraftWaterTrace(vec3 origin, vec3 dir) {
    float prevT = 0.0;
    float t = CRAFT_WATER_SSR_START;
    for (int i = 0; i < CRAFT_WATER_SSR_STEPS; i += CRAFT_WATER_SSR_BATCH) {
        // Issue a batch of depth fetches before testing any of them
        float ts[CRAFT_WATER_SSR_BATCH];
        vec4 clips[CRAFT_WATER_SSR_BATCH];
        vec2 uvs[CRAFT_WATER_SSR_BATCH];
        float depths[CRAFT_WATER_SSR_BATCH];
        for (int j = 0; j < CRAFT_WATER_SSR_BATCH; ++j) {
            ts[j] = t;
            clips[j] = uViewProj * vec4(origin + dir * t, 1.0);
            uvs[j] = ProjectToScreen(clips[j]).xy;
            depths[j] = textureLod(sceneDepth, uvs[j], 0.0).r;
            t = min(t * CRAFT_WATER_SSR_GROWTH + CRAFT_WATER_SSR_STEP_ADD,
                    CRAFT_WATER_SSR_MAX_DIST);
        }
        for (int j = 0; j < CRAFT_WATER_SSR_BATCH; ++j) {
            if (clips[j].w <= uSceneCamera.z || !CraftWaterOnScreen(uvs[j]))
                return vec4(0.0);
            float diff = clips[j].w - CraftWaterLinearDepth(depths[j]);
            float tolerance =
                max(CRAFT_WATER_SSR_MIN_THICKNESS,
                    (ts[j] - prevT) * CRAFT_WATER_SSR_THICKNESS_STEPS);
            if (depths[j] < 1.0 && diff > 0.0 && diff < tolerance) {
                float lo = prevT, hi = ts[j];
                vec2 uv = uvs[j];
                for (int k = 0; k < CRAFT_WATER_SSR_REFINE_STEPS; ++k) {
                    float mid = CRAFT_WATER_HALF * (lo + hi);
                    vec4 c = uViewProj * vec4(origin + dir * mid, 1.0);
                    vec3 m = ProjectToScreen(c);
                    float md = textureLod(sceneDepth, m.xy, 0.0).r;
                    if (c.w > CraftWaterLinearDepth(md)) {
                        hi = mid;
                        uv = m.xy;
                    } else {
                        lo = mid;
                    }
                }
                vec2 edge = min(uv, 1.0 - uv);
                float conf =
                    smoothstep(0.0, CRAFT_WATER_SSR_EDGE_FADE,
                               min(edge.x, edge.y)) *
                    (1.0 - smoothstep(CRAFT_WATER_SSR_DIST_FADE_START *
                                          CRAFT_WATER_SSR_MAX_DIST,
                                      CRAFT_WATER_SSR_MAX_DIST, hi));
                return vec4(CraftWaterHitFog(CraftWaterScene(uv),
                                             origin + dir * hi),
                            conf);
            }
            prevT = ts[j];
        }
    }
    return vec4(0.0);
}

/// BSL's dark water albedo lit like a voxel face with foliage scattering.
vec3 CraftWaterBody(vec3 faceN, vec3 V, vec3 L, float sky, float sunVis) {
    vec3 root = CRAFT_WATER_TINT * (CRAFT_WATER_TINT_GAIN *
                                    CRAFT_WATER_TINT_GAIN);
    CraftSurface s;
    s.albedo = root * root;
    s.ao = CRAFT_WATER_FULL_AO;
    s.normal = faceN;
    s.sky = sky;
    s.block = CRAFT_WATER_NO_LIGHT;
    s.emission = CRAFT_WATER_NO_LIGHT;
    s.sss = CRAFT_WATER_SSS;
    CraftLightSample l;
    l.toLight = L;
    l.toPixel = -V;
    l.shadow = sunVis;
    return CraftShadeSurface(s, l);
}

/// Sun or moon glint: BSL's near-mirror GGX on the light disc.
float CraftWaterGlint(vec3 N, vec3 V, vec3 L) {
    // Representative point: the disc direction closest to the mirror ray
    vec3 R = reflect(-V, N);
    vec3 toRay = dot(L, R) * R - L;
    L = normalize(L + toRay * min(CRAFT_WATER_LIGHT_TAN_RADIUS /
                                      max(length(toRay), CRAFT_WATER_EPSILON),
                                  1.0));
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return 0.0;
    float NdotV = max(dot(N, V), CRAFT_WATER_EPSILON);
    float a2 = CRAFT_WATER_ROUGHNESS * CRAFT_WATER_ROUGHNESS;
    // The disc's energy spread over the lobe (Karis)
    float disc = CRAFT_WATER_HALF * CRAFT_WATER_LIGHT_TAN_RADIUS;
    float spread = CRAFT_WATER_ROUGHNESS / (CRAFT_WATER_ROUGHNESS + disc);
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float dd = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (CRAFT_WATER_PI * dd * dd) * (spread * spread);
    float smithV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float smithL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    float vis = CRAFT_WATER_HALF / max(smithV + smithL, CRAFT_WATER_EPSILON);
    float F = CraftWaterFresnel(max(dot(V, H), 0.0));
    float s = D * vis * F * NdotL;
    return s / (1.0 + s / CRAFT_WATER_SPEC_SOFT_MAX);
}

/// Water at surface point P with face normal faceN and the sky light at the
/// surface (1 until phase 8 fills the vertex), before fog.
CraftWaterResult CraftShadeWater(vec3 P, vec3 faceN, float sky) {
    vec3 toCam = uFrame.CameraPos.xyz - P;
    float dist = length(toCam);
    vec3 V = toCam / max(dist, CRAFT_WATER_EPSILON);
    float facing = dot(faceN, V);
    bool below = facing < 0.0;
    bool top = faceN.y > CRAFT_WATER_TOP_FACE_MIN_Y;
    float footprint = max(length(dFdx(P.xz)), length(dFdy(P.xz)));
    vec4 wave = top ? CraftWaveNormal(P.xz, uFrame.Time, footprint, dist,
                                      abs(facing), below)
                    : vec4(faceN, 0.0);
    vec3 N = below ? -wave.xyz : wave.xyz;
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float F = CraftWaterFresnel(NdotV);
    vec3 L = normalize(uFrame.LightDir.xyz);
    float sunVis = CraftWaterSunVisibility();
    vec3 W = CraftWaterBody(faceN, V, L, sky, sunVis);
    vec2 uv = gl_FragCoord.xy * uScreenSize.zw;
    bool scene = uSceneFlags.x > CRAFT_WATER_HALF;
    vec3 R = reflect(-V, N);

    CraftWaterResult r;
    r.alpha = 1.0;
    if (below) {
        // Snell's window above, total internal reflection outside it
        float window0 = CRAFT_WATER_TIR_COS - CRAFT_WATER_TIR_SOFTEN;
        float window1 = CRAFT_WATER_TIR_COS + CRAFT_WATER_TIR_SOFTEN;
        F = max(F, 1.0 - smoothstep(window0, window1, NdotV));
        vec3 deep = SleakMediumActive() ? uAtmosphere.Medium.rgb : W;
        vec3 refl = deep;
        if (scene) {
            vec4 hit =
                CraftWaterTrace(P + N * CRAFT_WATER_SSR_ORIGIN_OFFSET, R);
            refl = mix(deep, hit.rgb, hit.a);
        }
        vec3 window = deep;
        vec3 up = refract(-V, N, CRAFT_WATER_IOR / CRAFT_AIR_IOR);
        if (dot(up, up) > 0.0) {
            float d0 = scene ? textureLod(sceneDepth, uv, 0.0).r : 1.0;
            window = d0 < 1.0 ? CraftWaterScene(uv)
                              : CraftWaterSky(up, 0.0) +
                                    SleakAtmosphereSun(up, up.y) +
                                    SleakAtmosphereMoon(up, up.y);
        }
        r.color = mix(window, refl, F);
        return r;
    }

    // Bed: the scene behind the surface, dimmed along the view ray in water
    vec3 bed = vec3(0.0);
    if (scene) {
        float d0 = textureLod(sceneDepth, uv, 0.0).r;
        if (d0 < 1.0) {
            float path = distance(P, Reconstruct(uv, d0));
            bed = max(CraftWaterScene(uv), vec3(0.0)) *
                  exp(-CRAFT_WATER_EXTINCTION * path);
        }
    }

    // Reflection: the screen trace over the sky probe, no open sky in caves
    vec3 refl = CraftWaterSky(R, sqrt(wave.w) * CRAFT_WATER_SKY_LOD_PER_SLOPE) *
                (sky * sky);
    float hitMask = 0.0;
    if (scene && F > CRAFT_WATER_SSR_MIN_FRESNEL) {
        vec3 dir =
            normalize(vec3(R.x, max(R.y, CRAFT_WATER_SSR_MIN_DIR_Y), R.z));
        vec4 hit = CraftWaterTrace(P + N * CRAFT_WATER_SSR_ORIGIN_OFFSET, dir);
        refl = mix(refl, hit.rgb, hit.a);
        hitMask = hit.a;
    }

    // Glint, kept off reflected geometry
    vec3 glint = uCraft.SunColor.rgb *
                 (uCraft.Sky.z * sunVis * sky * (1.0 - hitMask) *
                  CraftWaterGlint(N, V, L));

    // Veil over the bed in square-root space (BSL), glint on top
    vec3 surface = max(mix(W, refl, F), vec3(0.0));
    float alpha = mix(CRAFT_WATER_ALPHA, 1.0, F);
    if (!scene) {
        r.color = surface + glint / alpha;
        r.alpha = alpha;
        return r;
    }
    vec3 c = mix(sqrt(bed), sqrt(surface), alpha);
    r.color = c * c + glint;
    return r;
}

#endif
