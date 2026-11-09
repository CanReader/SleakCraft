#version 450 core
#extension GL_GOOGLE_include_directive : require

// SleakCraft deferred lighting, OpenGL: voxel pixels get CraftShadeVoxel
#include "sleak/lighting_default.glsl"
#include "craft/shading_models.glsl"

// Wrap lighting: N.L * scale + offset
const float CRAFT_WRAP_SCALE = 0.85;
const float CRAFT_WRAP_OFFSET = 0.15;
// N.L below which back faces get no direct light
const float CRAFT_SHADOW_FACE_FADE = 0.15;
// Ground bounce relative to the sky ambient
const vec3 CRAFT_GROUND_TINT = vec3(0.55, 0.50, 0.45);
// SSAO can at most halve the ambient term
const float CRAFT_SSAO_FLOOR = 0.5;
// Keeps the light range away from a zero divisor
const float CRAFT_RANGE_EPSILON = 1e-4;
// Light types (LightType in Light.hpp)
const uint CRAFT_LIGHT_DIRECTIONAL = 0u;
const uint CRAFT_LIGHT_POINT = 1u;

/// Voxel lighting of one non-sky pixel: every light in LightUBO (PCF for
/// directional), hemisphere ambient plus IBL, emissive, ACES, then fog.
vec3 CraftShadeVoxel(ivec2 px, vec2 uv, float depth) {
    vec4  albedoAO     = texture(gAlbedoAO,    uv);
    vec4  normalRough  = texture(gNormalRough, uv);
    vec4  metalEmit    = texture(gMetalEmit,   uv);

    vec3  albedo    = albedoAO.rgb;
    float ao        = albedoAO.a;
    if (SleakSSAOEnabled()) {
        float ssao = texture(gSSAO, uv).r;
        ao *= CRAFT_SSAO_FLOOR + CRAFT_SSAO_FLOOR * ssao;
    }
    vec3  N         = normalize(normalRough.rgb * 2.0 - 1.0);
    float roughness = normalRough.a;
    float metallic  = metalEmit.r;
    float emitScale = metalEmit.g;

    vec3 worldPos = SleakReconstructWorldPos(uv, depth);

    vec3 totalDiffuse = vec3(0.0);
    for (uint i = 0u; i < NumActiveLights; ++i) {
        SleakLightData light = Lights[i];

        vec3 L;
        float attenuation = 1.0;

        if (light.Type == CRAFT_LIGHT_DIRECTIONAL) {
            L = normalize(-light.Direction);
        } else if (light.Type == CRAFT_LIGHT_POINT) {
            vec3 toLight = light.Position - worldPos;
            float dist   = length(toLight);
            L            = toLight / dist;
            attenuation  = 1.0 / (1.0 + dist * dist /
                                  max(light.Range * light.Range,
                                      CRAFT_RANGE_EPSILON));
        } else {
            continue;
        }

        float rawNdotL = dot(N, L);
        float NdotL    = clamp(rawNdotL * CRAFT_WRAP_SCALE + CRAFT_WRAP_OFFSET,
                               0.0, 1.0);

        float shadow = 1.0;
        if (light.Type == CRAFT_LIGHT_DIRECTIONAL) {
            shadow = SleakCalcShadowPCF(worldPos, max(rawNdotL, 0.0));
            shadow *= smoothstep(0.0, CRAFT_SHADOW_FACE_FADE, rawNdotL);
        }

        totalDiffuse +=
            light.Color * light.Intensity * attenuation * NdotL * shadow;
    }

    // Hemisphere ambient, plus reflected environment light with IBL
    vec3  V             = normalize(CameraPos - worldPos);
    vec3  F0            = mix(SLEAK_DIELECTRIC_F0, albedo, metallic);
    vec3  skyAmbient    = AmbientColor * AmbientIntensity;
    vec3  groundAmbient = skyAmbient * CRAFT_GROUND_TINT;
    float hemisphere    = N.y * 0.5 + 0.5;
    vec3  hemAmbient    = mix(groundAmbient, skyAmbient, hemisphere);
    vec3  ambient       = albedo * hemAmbient;

    if (iblEnabled != 0u) {
        float NdotV_ibl       = max(dot(N, V), 0.0);
        vec3  kS_ibl          =
            SleakFresnelSchlickRoughness(NdotV_ibl, F0, roughness);
        vec3  kD_ibl          = (1.0 - kS_ibl) * (1.0 - metallic);
        vec3  irradiance      = texture(iblIrradiance, N).rgb;
        vec3  diffuseIBL      = irradiance * albedo;
        vec3  R               = reflect(-V, N);
        vec3  prefilteredColor= textureLod(iblPrefilter, R,
                                            roughness * maxReflectionLOD).rgb;
        vec2  envBRDF         = texture(iblBrdfLUT,
                                        vec2(NdotV_ibl, roughness)).rg;
        vec3  specularIBL     = prefilteredColor
                              * (kS_ibl * envBRDF.x + envBRDF.y);
        ambient += (kD_ibl * diffuseIBL + specularIBL) * iblIntensity;
    }
    ambient *= ao;

    vec3 color = ambient + albedo * totalDiffuse;
    color += albedo * emitScale;
    color = SleakACESFilm(color);
    return SleakApplyFog(color, worldPos);
}

void main() {
    float depth = texture(gDepth, fragUV).r;
    if (depth >= 1.0) discard;
    gl_FragDepth = depth;

    ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 color;
    if (SleakShadingModel(SleakReadMotionModel(px)) ==
        CRAFT_SHADING_MODEL_VOXEL)
        color = CraftShadeVoxel(px, fragUV, depth);
    else
        color = SleakShadeDefault(px, fragUV, depth);
    outColor = SleakFrameGuard(vec4(color, 1.0));
}
