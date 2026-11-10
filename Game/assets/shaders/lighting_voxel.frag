#version 450
#extension GL_GOOGLE_include_directive : require

// SleakCraft deferred lighting, Vulkan (Application::SetLightingShader).
// Voxel pixels get the composition in craft/lighting.glsl in linear scene
// units; engine materials keep SleakShadeDefault. The composite tonemaps.

#include "craft/lighting_pass.glsl"

/// Today's fog with its colors moved to scene units, so it displays as before.
vec3 CraftApplySceneFog(vec3 color, vec3 worldPos) {
    float amount = SleakFogAmount(worldPos);
    if (amount <= 0.0) return color;
    vec3 fog = SleakLegacyToScene(SleakFogColor(worldPos), SleakFrameOutput());
    return mix(color, fog, amount);
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(gDepth, px, 0).r;
    if (depth >= 1.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 rt3 = SleakReadMotionModel(px);
    vec3 color;
    if (CraftIsVoxelModel(SleakShadingModel(rt3))) {
        vec3 worldPos;
        color = CraftShadeVoxel(px, fragUV, depth, rt3, worldPos);
        color = CraftApplySceneFog(color, worldPos);
    } else {
        color = SleakShadeDefault(px, fragUV, depth);
    }
    outColor = SleakFrameGuard(vec4(color, 1.0));
}
