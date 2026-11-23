#version 450
#extension GL_GOOGLE_include_directive : require

// SleakCraft water, Vulkan: craft/water.glsl over the unfogged HDR snapshot;
// the atmosphere composite fogs the result at the water's depth.

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec4 fragShadowCoord;
layout(location = 4) in float fragSky;

layout(location = 0) out vec4 outColor;

#include "sleak/shadow_light.glsl"
#include "sleak/frame.glsl"
#include "sleak/atmosphere.glsl"

layout(set = 3, binding = 0) uniform sampler2DShadow shadowMap;

// Scene read (VertexLayoutDesc::transparentReadsScene)
layout(set = 3, binding = 2) uniform sampler2D sceneColor;  // HDR, unfogged
layout(set = 3, binding = 3) uniform sampler2D sceneDepth;
layout(set = 3, binding = 4) uniform samplerCube skyCube;   // probe, linear
layout(set = 3, binding = 5) uniform SceneReadUBO {
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uScreenSize;   // w, h, 1/w, 1/h
    vec4 uSceneCamera;  // xy = TAA jitter in NDC, z = near, w = far
    vec4 uSceneFlags;   // x = scene textures valid, y = sky valid
};

// Upper HDR clamp that keeps NaNs out of the bloom chain
#define WATER_MAX_HDR 1000.0
// Shadow map border the lookup fades out over (uv)
#define WATER_SHADOW_EDGE 0.05
#define WATER_FLAG_SET 0.5

// ---- Backend conventions (UV y down, depth = NDC z in [0, 1]) ----

/// Screen uv and depth of a clip position, jitter applied.
vec3 ProjectToScreen(vec4 clip) {
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = (vec2(ndc.x, -ndc.y) + uSceneCamera.xy) * 0.5 + 0.5;
    return vec3(uv, ndc.z);
}

/// World position of a screen uv and stored depth.
vec3 Reconstruct(vec2 uv, float depth) {
    vec2 ndc = uv * 2.0 - 1.0 - uSceneCamera.xy;
    vec4 w = uInvViewProj * vec4(ndc.x, -ndc.y, depth, 1.0);
    return w.xyz / w.w;
}

// ---- Water hooks ----

/// Snapshot in scene units (the HDR scene before fog).
vec3 CraftWaterScene(vec2 uv) { return textureLod(sceneColor, uv, 0.0).rgb; }

/// Sky probe (scene units); the gradient until the probe is bound.
vec3 CraftWaterSky(vec3 dir, float lod) {
    return uSceneFlags.y > WATER_FLAG_SET ? textureLod(skyCube, dir, lod).rgb
                                          : SleakAtmosphereSkyGradient(dir);
}

/// A trace hit seen through the fog or medium the composite gives the scene.
vec3 CraftWaterHitFog(vec3 color, vec3 hit) {
    vec3 rel = hit - uFrame.CameraPos.xyz;
    vec4 s = SleakMediumActive() ? SleakAtmosphereMedium(length(rel))
                                 : SleakAtmosphereScatter(rel, hit.y);
    return color * s.a + s.rgb;
}

/// Sun visibility: one hardware PCF tap, faded out at the map border.
float CraftWaterSunVisibility() {
    vec3 p = fragShadowCoord.xyz / fragShadowCoord.w;
    p.xy = p.xy * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0))))
        return 1.0;
    vec2 fade = smoothstep(vec2(0.0), vec2(WATER_SHADOW_EDGE), p.xy) *
                smoothstep(vec2(0.0), vec2(WATER_SHADOW_EDGE), 1.0 - p.xy);
    float lit = textureLod(shadowMap, vec3(p.xy, p.z - uShadowBias), 0.0);
    return mix(1.0, lit, uShadowStrength * fade.x * fade.y);
}

#include "craft/water.glsl"

void main() {
    CraftWaterResult w = CraftShadeWater(fragWorldPos, fragNormal, fragSky);
    outColor = SleakFrameGuard(
        vec4(clamp(w.color, vec3(0.0), vec3(WATER_MAX_HDR)), w.alpha));
}
