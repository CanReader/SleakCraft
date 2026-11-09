#ifndef CRAFT_LIGHTING_GLSL
#define CRAFT_LIGHTING_GLSL

#include "craft/frame.glsl"

// Face brightness: (up base + up slope * n.y + side spread * (side pivot -
// |n.x|) * (1 - |n.y|))^2, so top 1, z sides 0.72, x sides 0.49, bottom 0.25
#define CRAFT_FACE_UP_BASE 0.75
#define CRAFT_FACE_UP_SLOPE 0.25
#define CRAFT_FACE_SIDE_SPREAD 0.15
#define CRAFT_FACE_SIDE_PIVOT 0.667
// Terminator remap of N.L
#define CRAFT_NDOTL_GAIN 1.01
#define CRAFT_NDOTL_OFFSET 0.01
// Share of direct light rain removes
#define CRAFT_RAIN_SUN_LOSS 0.95
// Torch curve (level^power * gain + level * linear)^2
#define CRAFT_BLOCK_POWER 10.0
#define CRAFT_BLOCK_GAIN 1.6
#define CRAFT_BLOCK_LINEAR 0.6
// Emissive texels: tint toward white, flatten exponent
#define CRAFT_EMISSION_WHITE 0.5
#define CRAFT_FLATTEN_POWER 128.0
// Light night vision adds everywhere
#define CRAFT_NIGHT_VISION_LIGHT 0.25
// Foliage: wrap of N.L and forward scattering lobe
#define CRAFT_SSS_WRAP 0.7
#define CRAFT_SSS_LOBE_POWER 16.0
#define CRAFT_HALF 0.5
// Desaturation: rain keep, block light edges, cave grey, tint gain
#define CRAFT_DESAT_RAIN_KEEP 0.7
#define CRAFT_DESAT_BLOCK_LOW 0.25
#define CRAFT_DESAT_BLOCK_HIGH 1.0
#define CRAFT_DESAT_CAVE_GREY 0.4
#define CRAFT_DESAT_TINT_GAIN 1.7
// Rec. 601 luma
#define CRAFT_LUMA vec3(0.299, 0.587, 0.114)
// Keeps normalize away from a zero vector
#define CRAFT_COLOR_EPSILON 1e-5

/// Decoded voxel GBuffer texel.
struct CraftSurface {
    vec3 albedo;      // linear
    float ao;         // baked vertex AO
    vec3 normal;      // world, unit length
    float sky;        // sky light 0..1
    float block;      // block light 0..1
    float emission;   // 0..1
    float sss;        // foliage scattering 0..1
};

/// Light geometry and shadow map result of one pixel.
struct CraftLightSample {
    vec3 toLight;     // unit, world
    vec3 toPixel;     // unit, camera to pixel
    float shadow;     // shadow map visibility 0..1
};

/// Minecraft-like face brightness from the world normal.
float CraftFaceShade(vec3 n) {
    float f = CRAFT_FACE_UP_BASE + CRAFT_FACE_UP_SLOPE * n.y +
              CRAFT_FACE_SIDE_SPREAD * (CRAFT_FACE_SIDE_PIVOT - abs(n.x)) *
                  (1.0 - abs(n.y));
    return f * f;
}

/// True when the shadow map can change this pixel.
bool CraftNeedsShadow(CraftSurface s, vec3 toLight) {
    return dot(s.normal, toLight) > 0.0 || s.sss > 0.0;
}

/// Torch light at a block light level.
vec3 CraftBlockLight(float level) {
    float curve = pow(level, CRAFT_BLOCK_POWER) * CRAFT_BLOCK_GAIN +
                  level * CRAFT_BLOCK_LINEAR;
    return uCraft.BlockLightColor.rgb * (curve * curve);
}

/// 1 for emitters, which skip face shading and AO; 0 for other texels.
float CraftLightFlatten(float emission) {
    return 1.0 - pow(1.0 - emission, CRAFT_FLATTEN_POWER);
}

/// Self light of an emissive texel, tinted by its own hue.
vec3 CraftEmissionLight(vec3 albedo, float emission) {
    vec3 hue = normalize(albedo + CRAFT_COLOR_EPSILON);
    return mix(hue, vec3(1.0), emission * CRAFT_EMISSION_WHITE) * emission *
           uCraft.Look.y;
}

/// Pulls dark colors away from daylight toward a moon, rain or cave grey.
vec3 CraftDesaturate(vec3 color, CraftSurface s, float flatten) {
    float weight = uCraft.Look.x;
    if (weight <= 0.0) return color;
    float daylight = sqrt(s.sky) * uCraft.Sky.x *
                     (1.0 - CRAFT_DESAT_RAIN_KEEP * uCraft.Sky.y);
    float unlit = 1.0 - s.block;
    float amount = clamp(1.0 - daylight, 0.0, 1.0) *
                   smoothstep(CRAFT_DESAT_BLOCK_LOW, CRAFT_DESAT_BLOCK_HIGH,
                              unlit * unlit) *
                   (1.0 - flatten);
    vec3 outdoor = mix(uCraft.WeatherTint.rgb, uCraft.NightTint.rgb,
                       (1.0 - uCraft.Sky.x) * (1.0 - uCraft.Sky.y));
    vec3 tint = mix(vec3(CRAFT_DESAT_CAVE_GREY), outdoor, sqrt(s.sky)) *
                CRAFT_DESAT_TINT_GAIN;
    vec3 grey = mix(color, dot(color, CRAFT_LUMA) * tint, weight);
    return mix(color, grey, amount);
}

/// Lit voxel color in linear scene units, before SSAO and fog.
vec3 CraftShadeSurface(CraftSurface s, CraftLightSample l) {
    // Shadow, terminator
    float visibility = clamp(l.shadow, 0.0, 1.0);
    visibility *= mix(visibility, 1.0, s.sss);
    float nDotL = clamp(dot(s.normal, l.toLight), 0.0, 1.0);
    nDotL = clamp(nDotL * CRAFT_NDOTL_GAIN - CRAFT_NDOTL_OFFSET, 0.0, 1.0);

    // Foliage wrap and forward scattering
    float scatter = 0.0;
    if (s.sss > 0.0) {
        float forward = clamp(dot(l.toPixel, l.toLight) * CRAFT_HALF +
                                  CRAFT_HALF, 0.0, 1.0);
        scatter = pow(forward, CRAFT_SSS_LOBE_POWER) * (1.0 - uCraft.Sky.y) *
                  s.sss * uCraft.Sky.z;
        nDotL = mix(nDotL, 1.0, sqrt(s.sss) * CRAFT_SSS_WRAP);
        nDotL = mix(nDotL, 1.0, scatter);
    }

    // Sky: ambient where blocked, sun where lit
    float sunShare = visibility * nDotL *
                     (1.0 - CRAFT_RAIN_SUN_LOSS * uCraft.Sky.y) * uCraft.Sky.z;
    vec3 sky = mix(uCraft.AmbientColor.rgb * s.sky, uCraft.SunColor.rgb,
                   sunShare) *
               (s.sky * s.sky) * (1.0 + scatter * visibility);

    // Torch, emission, night vision, cave floor
    float flatten = CraftLightFlatten(s.emission);
    vec3 light = sky + CraftBlockLight(s.block) +
                 CraftEmissionLight(s.albedo, s.emission) +
                 vec3(uCraft.Sky.w * CRAFT_NIGHT_VISION_LIGHT) +
                 uCraft.MinLightColor.rgb * (1.0 - s.sky * s.sky);

    // Albedo balance, face shading, squared vertex AO
    float brightest = max(s.albedo.r, max(s.albedo.g, s.albedo.b));
    vec3 albedo =
        s.albedo / (1.0 + brightest * uCraft.Look.z * (1.0 - flatten));
    float face = mix(CraftFaceShade(s.normal), 1.0, flatten);
    float ao = mix(s.ao, 1.0, flatten);
    vec3 color = albedo * max(light, vec3(0.0)) * face * (ao * ao);
    return CraftDesaturate(color, s, flatten);
}

/// Screen-space AO multiplier of the whole lit color.
float CraftAmbientOcclusion(float ssao) {
    return pow(clamp(ssao, 0.0, 1.0), uCraft.Look.w);
}

#endif
