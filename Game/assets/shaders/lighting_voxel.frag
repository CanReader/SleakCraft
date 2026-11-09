#version 450
#extension GL_GOOGLE_include_directive : require

// SleakCraft deferred lighting, Vulkan: voxel pixels get CraftShadeVoxel
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
// Upper HDR clamp that keeps NaNs out of the bloom chain
const float CRAFT_MAX_HDR = 1000.0;

/// Voxel lighting of one non-sky pixel: wrap diffuse sun with PCF shadow,
/// hemisphere ambient, extra lights, emissive, fog. Linear HDR.
vec3 CraftShadeVoxel(ivec2 px, vec2 uv, float depth) {
    vec3  worldPos  = SleakReconstructWorldPos(uv, depth);
    vec4  albedoAO  = texelFetch(gAlbedoAO,    px, 0);
    vec4  normalRg  = texelFetch(gNormalRough, px, 0);
    vec4  metalEmit = texelFetch(gMetalEmit,   px, 0);

    vec3  albedo    = albedoAO.rgb;
    float bakedAO   = albedoAO.a;
    float ssao      = texture(gSSAO, uv).r;
    float ao        = bakedAO * ssao;
    vec3  N         = normalize(normalRg.rgb * 2.0 - 1.0);
    float roughness = normalRg.a;
    float metallic  = metalEmit.r;
    vec3  emissive  = metalEmit.gba;

    vec3  V     = normalize(uCameraPos.xyz - worldPos);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(SLEAK_DIELECTRIC_F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    vec3 ambient;
    if (iblEnabled != 0u) {
        // Cook-Torrance sun
        vec3  L    = normalize(-uLightDir.xyz);
        vec3  H    = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0) {
            float D = SleakDistributionGGX(N, H, roughness);
            float G = SleakGeometrySmith(N, V, L, roughness);
            vec3  F = SleakFresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            vec3 specular = (D * G * F) /
                            (4.0 * NdotV * NdotL + SLEAK_BRDF_EPSILON);

            float shadow = SleakCalcShadow(worldPos, N);
            shadow *= smoothstep(0.0, CRAFT_SHADOW_FACE_FADE, dot(N, L));

            vec3 lightColor = uLightColor.rgb * uLightColor.a;
            Lo = (kD * albedo / SLEAK_PI + specular) * lightColor * NdotL *
                 shadow;
        }

        // Extra directional lights, no shadows
        for (uint li = 0u; li < uNumExtraLights; ++li) {
            vec3  Lx    = normalize(-uExtraDir[li].xyz);
            vec3  Hx    = normalize(V + Lx);
            float NdotLx = max(dot(N, Lx), 0.0);
            if (NdotLx > 0.0) {
                float Dx = SleakDistributionGGX(N, Hx, roughness);
                float Gx = SleakGeometrySmith(N, V, Lx, roughness);
                vec3  Fx = SleakFresnelSchlick(max(dot(Hx, V), 0.0), F0);
                vec3  kSx = Fx;
                vec3  kDx = (vec3(1.0) - kSx) * (1.0 - metallic);
                vec3  specx = (Dx * Gx * Fx) /
                              (4.0 * NdotV * NdotLx + SLEAK_BRDF_EPSILON);
                vec3  lightColorx = uExtraColor[li].rgb * uExtraColor[li].a;
                Lo += (kDx * albedo / SLEAK_PI + specx) * lightColorx * NdotLx;
            }
        }

        // Split-sum IBL
        vec3 F_amb  = SleakFresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kS_amb = F_amb;
        vec3 kD_amb = (1.0 - kS_amb) * (1.0 - metallic);

        vec3 irradiance  = texture(iblIrradiance, N).rgb;
        vec3 diffuse_ibl = kD_amb * irradiance * albedo;

        vec3 R                = reflect(-V, N);
        vec3 prefilteredColor =
            textureLod(iblPrefilter, R, roughness * maxReflectionLOD).rgb;
        vec2 brdf             = texture(iblBrdfLUT, vec2(NdotV, roughness)).rg;
        vec3 specular_ibl     = prefilteredColor * (F_amb * brdf.x + brdf.y);

        ambient = (diffuse_ibl + specular_ibl) * ao * iblIntensity;
    } else {
        // Wrap diffuse sun and hemisphere ambient
        vec3 skyAmbient    = uAmbient.rgb * uAmbient.a;
        vec3 groundAmbient = skyAmbient * CRAFT_GROUND_TINT;
        float hemisphere   = N.y * 0.5 + 0.5;
        vec3  Ldir    = normalize(-uLightDir.xyz);
        float rawNdL  = dot(N, Ldir);
        float wrapNdL =
            clamp(rawNdL * CRAFT_WRAP_SCALE + CRAFT_WRAP_OFFSET, 0.0, 1.0);
        float shad    = 0.0;
        if (rawNdL > -CRAFT_SHADOW_FACE_FADE)
            shad = SleakCalcShadow(worldPos, N) *
                   smoothstep(-CRAFT_SHADOW_FACE_FADE, 0.0, rawNdL);
        vec3 direct   = uLightColor.rgb * uLightColor.a * wrapNdL * shad;
        for (uint li = 0u; li < uNumExtraLights; ++li) {
            vec3  Lx   = normalize(-uExtraDir[li].xyz);
            float NdLx = max(dot(N, Lx), 0.0);
            direct += uExtraColor[li].rgb * uExtraColor[li].a * NdLx;
        }
        float aoGL = bakedAO * (CRAFT_SSAO_FLOOR + CRAFT_SSAO_FLOOR * ssao);
        ambient = albedo * mix(groundAmbient, skyAmbient, hemisphere) * aoGL;
        Lo      = albedo * direct;
    }

    vec3 color = ambient + Lo + emissive;

    // Fog in linear HDR; the composite pass tonemaps
    color = SleakApplyFog(color, worldPos);
    return clamp(color, vec3(0.0), vec3(CRAFT_MAX_HDR));
}

void main() {
    ivec2 px    = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(gDepth, px, 0).r;
    if (depth >= 1.0) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }

    vec3 color;
    if (SleakShadingModel(SleakReadMotionModel(px)) ==
        CRAFT_SHADING_MODEL_VOXEL)
        color = CraftShadeVoxel(px, fragUV, depth);
    else
        color = SleakShadeDefault(px, fragUV, depth);
    outColor = SleakFrameGuard(vec4(color, 1.0));
}
