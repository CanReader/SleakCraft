#ifndef CRAFT_LIGHTING_PASS_GLSL
#define CRAFT_LIGHTING_PASS_GLSL

// Voxel half of SleakCraft's deferred lighting stems (lighting_voxel and
// lighting_voxel_gl); fog stays with each stem. Includes stay outside #ifdef:
// the OpenGL resolver expands them all.

#include "sleak/lighting_default.glsl"
#include "sleak/frame_output.glsl"
#include "craft/voxel_surface.glsl"
#include "craft/lighting.glsl"

// No foliage scattering until a foliage shading model exists
#define CRAFT_NO_SCATTERING 0.0
// N.L that leaves the OpenGL helper's back-face fade at 1
#define CRAFT_FULL_FACING 1.0
// Upper HDR clamp that keeps NaNs out of the bloom chain
#define CRAFT_MAX_HDR 1000.0

/// Voxel surface of one GBuffer texel.
CraftSurface CraftDecodeSurface(ivec2 px, vec4 rt3) {
    vec4 albedoAO = texelFetch(gAlbedoAO, px, 0);
    vec4 normalRough = texelFetch(gNormalRough, px, 0);
    vec4 metalEmit = texelFetch(gMetalEmit, px, 0);
    CraftSurface s;
    s.albedo = max(albedoAO.rgb, vec3(0.0));
    s.ao = clamp(albedoAO.a, 0.0, 1.0);
    s.normal = normalize(normalRough.rgb * 2.0 - 1.0);
    s.sky = CraftVoxelSkyLight(rt3);
    s.block = CraftVoxelBlockLight(metalEmit);
    s.emission = CraftVoxelEmission(metalEmit);
    s.sss = CRAFT_NO_SCATTERING;
    return s;
}

/// Raw sun visibility from the backend's shadow filter.
float CraftShadowVisibility(vec3 worldPos, vec3 n) {
#ifdef VULKAN
    return SleakCalcShadow(worldPos, n);
#else
    return SleakCalcShadowPCF(worldPos, CRAFT_FULL_FACING);
#endif
}

/// Voxel lighting of one non-sky pixel in linear scene units, before fog.
vec3 CraftShadeVoxel(ivec2 px, vec2 uv, float depth, vec4 rt3,
                     out vec3 worldPos) {
    worldPos = SleakReconstructWorldPos(uv, depth);
    CraftSurface s = CraftDecodeSurface(px, rt3);

    CraftLightSample l;
    l.toLight = normalize(uFrame.LightDir.xyz);
    l.toPixel = normalize(worldPos - uFrame.CameraPos.xyz);
    l.shadow = CraftNeedsShadow(s, l.toLight)
                   ? CraftShadowVisibility(worldPos, s.normal)
                   : 0.0;

    vec3 color = CraftShadeSurface(s, l);
    if (SleakSSAOEnabled())
        color *= CraftAmbientOcclusion(texture(gSSAO, uv).r);
    return clamp(color, vec3(0.0), vec3(CRAFT_MAX_HDR));
}

#endif
