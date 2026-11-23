#version 450 core
#extension GL_GOOGLE_include_directive : require

// SleakCraft water, OpenGL: craft/water.glsl over the stem's display
// output, taken back to scene units; fog, the output transform and the dither
// as the lighting stem applies them.

in vec3 fragWorldPos;
in vec3 fragNormal;
in vec4 fragColor;  // g: sky light at the vertex
in vec4 fragShadowCoord;

out vec4 outColor;

layout(std140, binding = 5) uniform ShadowUBO {
    mat4  ShadowLightVP;
    float ShadowBias;
    float ShadowStrength;
    float ShadowTexelSize;
    float ShadowLightSize;
    uint  PCSSEnabled;
    uint  ShadowMapEnabled;
    float _shadowPad0, _shadowPad1;
};

layout(binding = 3) uniform sampler2DShadow shadowMap;

#include "sleak/frame.glsl"
#include "sleak/frame_output.glsl"
#include "sleak/atmosphere_pass.glsl"

// Scene read (VertexLayoutDesc::transparentReadsScene)
layout(binding = 4) uniform sampler2D sceneColor;  // stem output, display
layout(binding = 5) uniform sampler2D sceneDepth;
layout(binding = 6) uniform samplerCube skyCube;   // probe, display
layout(std140, binding = 4) uniform SceneReadUBO {
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uScreenSize;   // w, h, 1/w, 1/h
    vec4 uSceneCamera;  // xy = jitter (unused on GL), z = near, w = far
    vec4 uSceneFlags;   // x = scene textures valid, y = sky valid
};

// Shadow map border the lookup fades out over (uv)
#define WATER_SHADOW_EDGE 0.05
#define WATER_FLAG_SET 0.5

// ---- Backend conventions (UV y up, window depth in [0, 1]) ----

/// Screen uv and window depth of a clip position.
vec3 ProjectToScreen(vec4 clip) {
    vec3 ndc = clip.xyz / clip.w;
    return ndc * 0.5 + 0.5;
}

/// World position of a screen uv and stored depth.
vec3 Reconstruct(vec2 uv, float depth) {
    vec4 w = uInvViewProj * vec4(uv * 2.0 - 1.0, SleakDepthToNdc(depth), 1.0);
    return w.xyz / w.w;
}

// ---- Water hooks ----

/// Snapshot in scene units: the stem's display output inverted.
vec3 CraftWaterScene(vec2 uv) {
    // shortcut: the snapshot keeps the stem's fog and medium, unfog if it shows
    return SleakOutputTransformInverse(textureLod(sceneColor, uv, 0.0).rgb,
                                       SleakFrameOutput());
}

/// Sky probe in scene units; the gradient until the probe is bound.
vec3 CraftWaterSky(vec3 dir, float lod) {
    if (uSceneFlags.y > WATER_FLAG_SET)
        return SleakOutputTransformInverse(textureLod(skyCube, dir, lod).rgb,
                                           SleakFrameOutput());
    return SleakAtmosphereSkyGradient(dir);
}

/// Trace hits come from the fogged snapshot already.
vec3 CraftWaterHitFog(vec3 color, vec3 hit) { return color; }

/// Sun visibility: one hardware PCF tap, faded out at the map border.
float CraftWaterSunVisibility() {
    if (ShadowMapEnabled == 0u) return 1.0;
    vec3 p = fragShadowCoord.xyz / fragShadowCoord.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0))))
        return 1.0;
    vec2 fade = smoothstep(vec2(0.0), vec2(WATER_SHADOW_EDGE), p.xy) *
                smoothstep(vec2(0.0), vec2(WATER_SHADOW_EDGE), 1.0 - p.xy);
    float lit = textureLod(shadowMap, vec3(p.xy, p.z - ShadowBias), 0.0);
    return mix(1.0, lit, ShadowStrength * fade.x * fade.y);
}

#include "craft/water.glsl"

void main() {
    CraftWaterResult w = CraftShadeWater(fragWorldPos, fragNormal, fragColor.g);
    SleakOutputParams o = SleakFrameOutput();
    vec4 scatter =
        SleakAtmosphereGuard(SleakAtmosphereScatterWorld(fragWorldPos));
    vec3 display =
        SleakOutputTransform(SleakAtmosphereApply(w.color, scatter), o);
    display = SleakApplyDither(display, gl_FragCoord.xy, o.dither);
    outColor = SleakFrameGuard(vec4(display, w.alpha));
}
