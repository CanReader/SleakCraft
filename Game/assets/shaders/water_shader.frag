#version 450

layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in vec3  fragNormal;
layout(location = 3) in vec4  fragShadowCoord;
layout(location = 4) in float fragTime;

layout(location = 0) out vec4 outColor;

// NOTE: see water_shader.vert — no MaterialUBO in the Vulkan layout.
// Time comes from uCameraPos.w (interpolated via fragTime).
layout(set = 2, binding = 0) uniform ShadowLightUBO {
    vec4  uLightDir;
    vec4  uLightColor;
    vec4  uAmbient;
    vec4  uCameraPos;
    mat4  uLightVP;
    float uShadowBias;
    float uShadowStrength;
    float uShadowTexelSize;
    float uLightSize;
    vec4  uFogColor;       // horizon
    float uFogStart;
    float uFogEnd;
    vec2  _fogPad;         // std140: must be vec2 (8B), NOT float[2] (stride-16 = 32B)
    vec4  uFogZenithColor; // zenith
    float uHeightFogTop;
    float uHeightFogDensity;
    float uHeightFogFalloff;
    float uHeightFogEnabled;
};

layout(set = 3, binding = 0) uniform sampler2DShadow shadowMap;

// Scene read (VertexLayoutDesc::transparentReadsScene)
layout(set = 3, binding = 2) uniform sampler2D   sceneColor;  // linear HDR
layout(set = 3, binding = 3) uniform sampler2D   sceneDepth;
layout(set = 3, binding = 4) uniform samplerCube skyCube;
layout(set = 3, binding = 5) uniform SceneReadUBO {
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uScreenSize;   // w, h, 1/w, 1/h
    vec4 uSceneCamera;  // xy = TAA jitter in NDC, z = near, w = far
    vec4 uSceneFlags;   // x = scene textures valid, y = sky valid
};

#define EPSILON 1e-4

float SignedToUnit(float v) { return v * 0.5 + 0.5; }
vec2  SignedToUnit(vec2 v)  { return v * 0.5 + 0.5; }
vec2  UnitToSigned(vec2 v)  { return v * 2.0 - 1.0; }
bool  IsSet(float flag)     { return flag > 0.5; }

// ============================================================
// Backend conventions (Vulkan: UV y down, depth = NDC z in [0,1])
// ============================================================
vec3 ProjectToScreen(vec4 clip) {
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv  = SignedToUnit(vec2(ndc.x, -ndc.y) + uSceneCamera.xy);
    return vec3(uv, ndc.z);
}
vec3 Reconstruct(vec2 uv, float depth) {
    vec2 ndc = UnitToSigned(uv) - uSceneCamera.xy;
    vec4 w   = uInvViewProj * vec4(ndc.x, -ndc.y, depth, 1.0);
    return w.xyz / w.w;
}
float DepthToNdc(float depth) { return depth; }
vec3 SceneToLinear(vec3 c) { return c; }
vec3 SkyRadiance(vec3 dir, float lod) {
    return textureLod(skyCube, dir, lod).rgb;
}

// ============================================================
// Shadow — 16-sample PCF with IGN rotation (matches GL/DX11/DX12)
// ============================================================
float InterleavedGradientNoise(vec2 p) {
    vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(p, m.xy)));
}
const vec2 disk[16] = vec2[](
    vec2(-0.9465,-0.1484), vec2(-0.7431, 0.5353), vec2(-0.5863,-0.5879), vec2(-0.3935, 0.1025),
    vec2(-0.2428, 0.7722), vec2(-0.1074,-0.3075), vec2( 0.0542,-0.8645), vec2( 0.1267, 0.4300),
    vec2( 0.2787,-0.1353), vec2( 0.3842, 0.6501), vec2( 0.4714,-0.5537), vec2( 0.5765, 0.1675),
    vec2( 0.6712,-0.3340), vec2( 0.7527, 0.4813), vec2( 0.8745,-0.0910), vec2( 0.9601, 0.2637)
);
float CalcShadow(vec4 sc) {
    vec3 p = sc.xyz / sc.w;
    p.xy = p.xy * 0.5 + 0.5;
    // Vulkan clip-space Z is already [0,1] — no remap needed (unlike GL).
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z < 0.0 || p.z > 1.0) return 1.0;
    vec2 fc = smoothstep(vec2(0.0), vec2(0.05), p.xy)
            * smoothstep(vec2(0.0), vec2(0.05), vec2(1.0) - p.xy);
    float angle = InterleavedGradientNoise(p.xy / uShadowTexelSize) * 6.28318530;
    float sa = sin(angle), ca = cos(angle);
    mat2  rot = mat2(ca, sa, -sa, ca);
    float rad = uShadowTexelSize * uLightSize * 6.0;
    float s = 0.0;
    for (int i = 0; i < 16; i++)
        s += textureLod(shadowMap, vec3(p.xy + rot * disk[i] * rad, p.z - uShadowBias), 0.0);
    return mix(1.0, s / 16.0, uShadowStrength * fc.x * fc.y);
}

// ============================================================
// Lighting inputs
// ============================================================
vec3 SunDirection() { return normalize(-uLightDir.xyz); }
vec3 SunRadiance()  { return uLightColor.rgb * uLightColor.a; }
vec3 AmbientRadiance() { return uAmbient.rgb * uAmbient.a; }
vec3 CameraPosition()  { return uCameraPos.xyz; }

// Fog terms of lighting_pass ApplyFog, split so the snapshot can be unfogged
float FogAmount(vec3 p) {
    if (uFogEnd <= 0.0) return 0.0;
    float dist = length(p.xz - uCameraPos.xz);
    float distFog = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, EPSILON), 0.0, 1.0);
    float heightFog = 0.0;
    if (IsSet(uHeightFogEnabled)) {
        float h = max(uHeightFogTop - p.y, 0.0);
        heightFog = clamp(uHeightFogDensity * (1.0 - exp(-h * uHeightFogFalloff)),
                          0.0, 1.0);
    }
    return 1.0 - (1.0 - distFog) * (1.0 - heightFog);
}
vec3 FogColorAt(vec3 p) {
    vec3 viewDir = normalize(p - uCameraPos.xyz);
    float t = smoothstep(0.0, 1.0, clamp(SignedToUnit(viewDir.y), 0.0, 1.0));
    return mix(uFogColor.rgb, uFogZenithColor.rgb, t);
}

// ==== SHARED WATER BODY (keep identical in water_shader_gl.frag) ====

// ---- Physical constants ----
#define PI          3.14159265
#define TWO_PI      (2.0 * PI)
#define GRAVITY     9.81   // m/s^2
#define WATER_IOR   1.333
#define AIR_IOR     1.0
#define FRESNEL_F0  pow((WATER_IOR - AIR_IOR) / (WATER_IOR + AIR_IOR), 2.0)
// Cosine of the total internal reflection angle seen from below
#define TIR_COS     sqrt(1.0 - pow(AIR_IOR / WATER_IOR, 2.0))
#define TIR_SOFTEN  0.04

// ---- Waves: octaves derived from a few parameters ----
#define WAVE_COUNT           12
#define WAVE_BASE_LENGTH     13.0     // m, longest octave
#define WAVE_LENGTH_RATIO    0.7      // each octave's wavelength over the previous
#define WAVE_BASE_STEEPNESS  0.075    // slope amplitude of the longest octave
#define WAVE_STEEPNESS_DECAY 0.8946   // per-octave steepness falloff
#define WIND_ANGLE           0.2      // rad, mean travel direction in xz
#define WIND_SPREAD          1.4      // rad, octaves spread across +-this
#define GOLDEN_ANGLE         (PI * (3.0 - sqrt(5.0)))    // decorrelates directions
#define GOLDEN_RATIO_FRAC    ((sqrt(5.0) - 1.0) * 0.5)   // decorrelates phases
#define WAVE_SPEED_SCALE     0.55     // fraction of deep-water phase speed
#define WAVE_FADE_START      0.08     // pixel footprint / wavelength
#define WAVE_FADE_END        0.25     // 4 pixels per wavelength
#define SWELL_BASE        0.8
#define SWELL_RANGE       0.2
#define SWELL_FREQ_A      vec2(0.021, 0.013)  // rad/m
#define SWELL_FREQ_B      vec2(-0.011, 0.024)
#define SWELL_SPEED_A     0.07                // rad/s
#define SWELL_SPEED_B     0.05

// ---- Optics ----
#define BASE_ROUGHNESS     0.035
#define SIGMA_T            vec3(0.46, 0.105, 0.075)  // absorption + scattering, 1/m
#define SCATTER_ALBEDO     vec3(0.014, 0.080, 0.150)
#define MIN_TRANSMIT_COS   0.2     // caps the slant path at grazing angles
#define SPEC_MAX           30.0
#define SUN_SHADOW_FADE    vec2(-0.1, 0.2)  // N.L range where the sun fades in
#define MAX_UNFOG          0.9

// ---- Sky ----
#define SKY_MIN_Y          0.07    // stay above the skybox's own horizon band
#define SKY_MAX_RADIANCE   16.0
#define SKY_LOD_PER_SLOPE  10.0
#define SKY_HORIZON        vec3(0.70, 0.82, 0.95)
#define SKY_ZENITH         vec3(0.22, 0.42, 0.82)
#define SKY_HORIZON_TINT   0.30
#define SKY_ZENITH_TINT    0.25
#define SKY_ZENITH_DIM     0.65
#define SKY_GRADIENT_POW   0.45
#define SKY_SUN_POW        60.0
#define SKY_SUN_GAIN       0.5

// ---- Refraction and bottom ----
#define REFRACT_SCALE       1.2    // m of bottom shift per unit slope and m depth
#define REFRACT_MAX_DEPTH   2.5
#define REFRACT_DEPTH_BIAS  0.05   // m the bent sample must sit behind the surface
#define ABSENT_DEPTH        1e3    // depth used when nothing lies behind the water
#define BLUR_PER_DEPTH      0.06   // m of blur radius per m of depth
#define BLUR_MIN_PX         0.5
#define BLUR_MAX_PX         6.0
#define BLUR_TAPS           4.0
#define BLUR_CENTER_WEIGHT  (1.0 / (BLUR_TAPS + 1.0))
#define CAUSTIC_SCALE       0.9
#define CAUSTIC_STRENGTH    1.2
#define CAUSTIC_FALLOFF     0.35   // 1/m
#define CAUSTIC_FADE_IN     vec2(0.05, 0.5)
#define CAUSTIC_SHARPNESS   5.0
#define MIN_SUN_ELEVATION   0.2

// ---- Screen-space reflection ----
#define SSR_MIN_FRESNEL     0.04
#define SSR_STEPS           16
#define SSR_BATCH           4      // depth fetches issued together
#define SSR_REFINE_STEPS    4
#define SSR_START           0.3    // m
#define SSR_START_JITTER    0.4    // m
#define SSR_GROWTH          1.38
#define SSR_STEP_ADD        0.1    // m
#define SSR_MAX_DIST        90.0   // m
#define SSR_MIN_THICKNESS   0.75   // m
#define SSR_THICKNESS_STEPS 2.0    // hit tolerance in step lengths
#define SSR_EDGE_FADE       0.07   // UV
#define SSR_DIST_FADE_START 0.6    // fraction of SSR_MAX_DIST
#define SSR_ORIGIN_OFFSET   0.02   // m along the normal
#define SSR_MIN_DIR_Y       0.015
#define SSR_FRAME_CYCLE     16.0
#define SSR_NOISE_SHIFT     vec2(5.588, 0.0)
#define FRAMES_PER_SECOND   60.0

// ---- Shoreline ----
#define SHORE_FADE_DEPTH    0.12   // m of depth over which the contact line fades in
#define FOAM_MAX_DEPTH      0.7
#define FOAM_BAND           0.6
#define FOAM_SCALE          3.0    // 1/m
#define FOAM_DRIFT          vec2(0.21, -0.17)
#define FOAM_DETAIL_SCALE   2.3
#define FOAM_DETAIL_OFFSET  7.1
#define FOAM_DETAIL_WEIGHT  0.35
#define FOAM_THRESHOLD      vec2(0.5, 0.85)
#define FOAM_NOISE_WEIGHT   0.7
#define FOAM_SHORE_WEIGHT   0.55
#define FOAM_OPACITY        0.8
#define FOAM_ALBEDO         0.85

// ---- Faces ----
#define TOP_FACE_MIN_Y      0.5    // normal.y above which a face is the water top

// ---- Hash (Dave Hoskins, hash without sine) ----
#define HASH_SCALE  0.1031
#define HASH_OFFSET 33.33

// ---- Caustics: two warped sine layers (x = layer A, y = layer B) ----
#define CAUSTIC_FREQ        vec2(1.7, 1.9)
#define CAUSTIC_WARP_FREQ   vec2(1.3, 1.1)
#define CAUSTIC_WARP_AMOUNT vec2(1.2, 1.3)
#define CAUSTIC_WARP_SPEED  vec2(0.8, -0.7)
#define CAUSTIC_DRIFT       vec2(0.6, -0.5)

// xyz = normal, w = slope variance removed by the filter
vec4 WaveNormal(vec2 xz, float t, float footprint) {
    float swell = SWELL_BASE + SWELL_RANGE
                * sin(dot(xz, SWELL_FREQ_A) + t * SWELL_SPEED_A)
                * sin(dot(xz, SWELL_FREQ_B) - t * SWELL_SPEED_B);
    vec2  g = vec2(0.0);
    float lost = 0.0;
    float wavelength = WAVE_BASE_LENGTH;
    float steepness = WAVE_BASE_STEEPNESS * swell;
    for (int i = 0; i < WAVE_COUNT; ++i) {
        float fade = 1.0 - smoothstep(WAVE_FADE_START, WAVE_FADE_END, footprint / wavelength);
        lost += steepness * steepness * (1.0 - fade * fade);
        if (fade > 0.0) {
            float angle = WIND_ANGLE + WIND_SPREAD * sin(float(i) * GOLDEN_ANGLE);
            vec2  dir = vec2(cos(angle), sin(angle));
            float k = TWO_PI / wavelength;
            float omega = sqrt(GRAVITY * k) * WAVE_SPEED_SCALE;
            float phase = fract(float(i) * GOLDEN_RATIO_FRAC) * TWO_PI;
            g += dir * (steepness * fade * cos(dot(dir, xz) * k - omega * t + phase));
        }
        wavelength *= WAVE_LENGTH_RATIO;
        steepness *= WAVE_STEEPNESS_DECAY;
    }
    // Mean of cos^2 is one half
    return vec4(normalize(vec3(-g.x, 1.0, -g.y)), 0.5 * lost);
}

float FresnelSchlick(float c) {
    return FRESNEL_F0 + (1.0 - FRESNEL_F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0);
}

vec3 SkyColor(vec3 dir, float lod, vec3 sunDir, vec3 sunCol, vec3 amb) {
    dir = normalize(vec3(dir.x, max(dir.y, SKY_MIN_Y), dir.z));
    if (IsSet(uSceneFlags.y)) return min(SkyRadiance(dir, lod), vec3(SKY_MAX_RADIANCE));
    vec3 horiz  = mix(SKY_HORIZON, amb, SKY_HORIZON_TINT);
    vec3 zenith = mix(SKY_ZENITH, amb * SKY_ZENITH_DIM, SKY_ZENITH_TINT);
    vec3 sky = mix(horiz, zenith, pow(dir.y, SKY_GRADIENT_POW));
    float sd = max(dot(dir, sunDir), 0.0);
    return sky + sunCol * pow(sd, SKY_SUN_POW) * SKY_SUN_GAIN;
}

float Hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * HASH_SCALE);
    p3 += dot(p3, p3.yzx + HASH_OFFSET);
    return fract((p3.x + p3.y) * p3.z);
}
float ValueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash12(i), Hash12(i + vec2(1.0, 0.0)), u.x),
               mix(Hash12(i + vec2(0.0, 1.0)), Hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

float Caustics(vec2 p, float t) {
    vec2 warp = sin(p.yx * CAUSTIC_WARP_FREQ + t * CAUSTIC_WARP_SPEED) * CAUSTIC_WARP_AMOUNT;
    vec2 layers = abs(sin(p * CAUSTIC_FREQ + warp + t * CAUSTIC_DRIFT));
    float a = layers.x, b = layers.y;
    // Bright where both layers cross zero
    return pow(clamp(1.0 - 0.5 * (a + b), 0.0, 1.0), CAUSTIC_SHARPNESS);
}

vec3 Unfog(vec3 c, vec3 p) {
    float f = min(FogAmount(p), MAX_UNFOG);
    return max((c - FogColorAt(p) * f) / (1.0 - f), vec3(0.0));
}

float LinearDepth(float depth) {
    float n = uSceneCamera.z, f = uSceneCamera.w;
    return n * f / max(f - DepthToNdc(depth) * (f - n), EPSILON);
}

bool OnScreen(vec2 uv) {
    return all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)));
}

// rgb = scene color at the hit, a = confidence
vec4 TraceReflection(vec3 origin, vec3 dir, float jitter) {
    float prevT = 0.0;
    float t = SSR_START + SSR_START_JITTER * jitter;
    for (int i = 0; i < SSR_STEPS; i += SSR_BATCH) {
        // Issue a batch of depth fetches before testing any of them
        float ts[SSR_BATCH];
        vec4  clips[SSR_BATCH];
        vec2  uvs[SSR_BATCH];
        float depths[SSR_BATCH];
        for (int j = 0; j < SSR_BATCH; ++j) {
            ts[j] = t;
            clips[j] = uViewProj * vec4(origin + dir * t, 1.0);
            uvs[j] = ProjectToScreen(clips[j]).xy;
            depths[j] = textureLod(sceneDepth, uvs[j], 0.0).r;
            t = min(t * SSR_GROWTH + SSR_STEP_ADD, SSR_MAX_DIST);
        }
        for (int j = 0; j < SSR_BATCH; ++j) {
            if (clips[j].w <= uSceneCamera.z || !OnScreen(uvs[j])) return vec4(0.0);
            float diff = clips[j].w - LinearDepth(depths[j]);
            float tolerance = max(SSR_MIN_THICKNESS, (ts[j] - prevT) * SSR_THICKNESS_STEPS);
            if (depths[j] < 1.0 && diff > 0.0 && diff < tolerance) {
                float lo = prevT, hi = ts[j];
                vec2 uv = uvs[j];
                for (int k = 0; k < SSR_REFINE_STEPS; ++k) {
                    float mid = 0.5 * (lo + hi);
                    vec4 c = uViewProj * vec4(origin + dir * mid, 1.0);
                    vec3 m = ProjectToScreen(c);
                    float md = textureLod(sceneDepth, m.xy, 0.0).r;
                    if (c.w > LinearDepth(md)) { hi = mid; uv = m.xy; } else { lo = mid; }
                }
                vec2  edge = min(uv, 1.0 - uv);
                float conf = smoothstep(0.0, SSR_EDGE_FADE, min(edge.x, edge.y))
                           * (1.0 - smoothstep(SSR_DIST_FADE_START * SSR_MAX_DIST, SSR_MAX_DIST, hi));
                return vec4(textureLod(sceneColor, uv, 0.0).rgb, conf);
            }
            prevT = ts[j];
        }
    }
    return vec4(0.0);
}

struct WaterResult {
    vec3  color;     // composited water, linear, before fog
    vec3  backdrop;  // unrefracted scene behind the surface, as stored
    float edge;      // 0 at the contact line, 1 once the water has depth
};

WaterResult ShadeWater(vec3 P, vec3 Nface, float t, float footprint) {
    WaterResult r;
    vec3  sunDir = SunDirection();
    vec3  sunCol = SunRadiance();
    vec3  amb    = AmbientRadiance();
    vec3  toCam  = CameraPosition() - P;
    vec3  V      = normalize(toCam);
    bool  top    = Nface.y > TOP_FACE_MIN_Y;
    bool  below  = dot(Nface, V) < 0.0;
    bool  scene  = IsSet(uSceneFlags.x);

    // Waves
    vec4 wave = top ? WaveNormal(P.xz, t, footprint) : vec4(Nface, 0.0);
    vec3 N = below ? -wave.xyz : wave.xyz;
    float a2 = BASE_ROUGHNESS * BASE_ROUGHNESS + wave.w;

    float NdotV  = max(dot(N, V), 0.0);
    float fresnel = FresnelSchlick(NdotV);

    // Sun visibility at the surface
    float NdotS = dot(N, sunDir);
    float sunVis = 0.0;
    if (!below && NdotS > SUN_SHADOW_FADE.x)
        sunVis = CalcShadow(fragShadowCoord) * smoothstep(SUN_SHADOW_FADE.x, SUN_SHADOW_FADE.y, NdotS);
    vec3 lightIn  = amb + sunCol * (sunVis * max(sunDir.y, 0.0));
    vec3 inscatter = SCATTER_ALBEDO * lightIn;

    // Scene behind the surface
    vec2  uv0 = gl_FragCoord.xy * uScreenSize.zw;
    vec3  backdrop = vec3(0.0);
    vec3  under = inscatter;
    float depth0 = ABSENT_DEPTH;
    if (scene) {
        float d0 = textureLod(sceneDepth, uv0, 0.0).r;
        vec3  B0 = Reconstruct(uv0, d0);
        backdrop = textureLod(sceneColor, uv0, 0.0).rgb;
        if (d0 < 1.0) depth0 = top ? max(P.y - B0.y, 0.0) : distance(P, B0);

        // Refraction: bend the bottom lookup by the wave slope, keep it behind the surface
        vec2  uv1 = uv0;
        vec3  B1 = B0;
        float d1 = d0;
        if (top && !below && d0 < 1.0) {
            vec3 bent = B0 + vec3(N.x, 0.0, N.z) * (REFRACT_SCALE * min(depth0, REFRACT_MAX_DEPTH));
            vec4 bc = uViewProj * vec4(bent, 1.0);
            if (bc.w > uSceneCamera.z) {
                vec3 s = ProjectToScreen(bc);
                float sd = textureLod(sceneDepth, s.xy, 0.0).r;
                float surfZ = (uViewProj * vec4(P, 1.0)).w;
                if (OnScreen(s.xy) && LinearDepth(sd) > surfZ + REFRACT_DEPTH_BIAS) {
                    uv1 = s.xy;
                    d1 = sd;
                    B1 = Reconstruct(uv1, sd);
                }
            }
        }
        float depth1 = d1 < 1.0 ? (top ? max(P.y - B1.y, 0.0) : distance(P, B1)) : ABSENT_DEPTH;

        // Beer-Lambert along the refracted view ray plus the sunlight's way down
        float cosV = clamp(abs(dot(Nface, V)), 0.0, 1.0);
        float cosT = sqrt(1.0 - (1.0 - cosV * cosV) / (WATER_IOR * WATER_IOR));
        float path = below ? length(toCam) : depth1 * (1.0 / max(cosT, MIN_TRANSMIT_COS) + 1.0);
        vec3  trans = exp(-SIGMA_T * path);

        // Scattering softens the bottom with depth
        vec3 bottom = textureLod(sceneColor, uv1, 0.0).rgb;
        float blurPx = clamp(depth1 * BLUR_PER_DEPTH / max(footprint, EPSILON), 0.0, BLUR_MAX_PX);
        if (blurPx > BLUR_MIN_PX) {
            float ang = InterleavedGradientNoise(gl_FragCoord.xy) * (0.5 * PI);
            vec2  o1 = vec2(cos(ang), sin(ang)) * blurPx * uScreenSize.zw;
            vec2  o2 = vec2(-o1.y, o1.x);
            bottom = (bottom +
                      textureLod(sceneColor, uv1 + o1, 0.0).rgb +
                      textureLod(sceneColor, uv1 - o1, 0.0).rgb +
                      textureLod(sceneColor, uv1 + o2, 0.0).rgb +
                      textureLod(sceneColor, uv1 - o2, 0.0).rgb) * BLUR_CENTER_WEIGHT;
        }
        bottom = SceneToLinear(Unfog(bottom, B1));
        if (top && !below && d1 < 1.0) {
            vec2 cp = B1.xz + sunDir.xz * (depth1 / max(sunDir.y, MIN_SUN_ELEVATION));
            float c = Caustics(cp * CAUSTIC_SCALE, t);
            bottom *= 1.0 + c * CAUSTIC_STRENGTH * sunVis * exp(-CAUSTIC_FALLOFF * depth1)
                    * smoothstep(CAUSTIC_FADE_IN.x, CAUSTIC_FADE_IN.y, depth1);
        }
        under = bottom * trans + inscatter * (1.0 - trans);
    }

    // Reflection: screen-space hit over the sky cubemap
    vec3 R = reflect(-V, N);
    vec3 refl = SkyColor(R, sqrt(wave.w) * SKY_LOD_PER_SLOPE, sunDir, sunCol, amb);
    if (scene && !below && fresnel > SSR_MIN_FRESNEL) {
        float frame = mod(t * FRAMES_PER_SECOND, SSR_FRAME_CYCLE);
        float jitter = InterleavedGradientNoise(gl_FragCoord.xy + SSR_NOISE_SHIFT * frame);
        vec3  dir = normalize(vec3(R.x, max(R.y, SSR_MIN_DIR_Y), R.z));
        vec4  hit = TraceReflection(P + N * SSR_ORIGIN_OFFSET, dir, jitter);
        refl = mix(refl, SceneToLinear(hit.rgb), hit.a);
    }
    if (below) {
        refl = inscatter;
        fresnel = max(fresnel, 1.0 - smoothstep(TIR_COS - TIR_SOFTEN, TIR_COS + TIR_SOFTEN, NdotV));
    }

    // Sun glint (GGX, widened by the filtered-out wave slopes)
    vec3 spec = vec3(0.0);
    if (!below && NdotS > 0.0) {
        vec3  H = normalize(V + sunDir);
        float NdotH = max(dot(N, H), 0.0);
        float dd = NdotH * NdotH * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * dd * dd);
        float smithV = NdotS * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
        float smithL = NdotV * sqrt(NdotS * NdotS * (1.0 - a2) + a2);
        float vis = 0.5 / max(smithV + smithL, EPSILON);
        float F = FresnelSchlick(max(dot(V, H), 0.0));
        spec = sunCol * min(D * vis * F * NdotS * sunVis, SPEC_MAX);
    }

    vec3 color = mix(under, refl, fresnel) + spec;

    // Shoreline foam
    float foam = 0.0;
    if (scene && top && !below && depth0 < FOAM_MAX_DEPTH) {
        float shore = 1.0 - smoothstep(0.0, FOAM_BAND, depth0);
        vec2  fp = P.xz * FOAM_SCALE + FOAM_DRIFT * t;
        float n = mix(ValueNoise(fp), ValueNoise(fp * FOAM_DETAIL_SCALE + FOAM_DETAIL_OFFSET),
                      FOAM_DETAIL_WEIGHT);
        foam = smoothstep(FOAM_THRESHOLD.x, FOAM_THRESHOLD.y,
                          n * FOAM_NOISE_WEIGHT + shore * FOAM_SHORE_WEIGHT) * shore * FOAM_OPACITY;
        color = mix(color, FOAM_ALBEDO * lightIn, foam);
    }

    r.color = color;
    r.backdrop = backdrop;
    r.edge = (scene && !below) ? max(smoothstep(0.0, SHORE_FADE_DEPTH, depth0), foam) : 1.0;
    return r;
}

// Opacity of the plain blended fallback when the scene cannot be read
#define FALLBACK_ALPHA       vec2(0.80, 0.97)
#define FALLBACK_ALPHA_POW   2.0

float FallbackAlpha(vec3 P, vec3 Nface) {
    float NdotV = abs(dot(Nface, normalize(CameraPosition() - P)));
    return mix(FALLBACK_ALPHA.x, FALLBACK_ALPHA.y, pow(1.0 - NdotV, FALLBACK_ALPHA_POW));
}

// ==== END SHARED WATER BODY ====

#define MAX_HDR 1000.0

void main() {
    vec3 P = fragWorldPos;
    float footprint = max(length(dFdx(P.xz)), length(dFdy(P.xz)));
    WaterResult w = ShadeWater(P, fragNormal, fragTime, footprint);

    vec3 color = mix(w.color, FogColorAt(P), FogAmount(P));
    float alpha = 1.0;
    if (IsSet(uSceneFlags.x))
        color = mix(w.backdrop, color, w.edge);
    else
        alpha = FallbackAlpha(P, fragNormal);
    outColor = vec4(clamp(color, vec3(0.0), vec3(MAX_HDR)), alpha);
}
