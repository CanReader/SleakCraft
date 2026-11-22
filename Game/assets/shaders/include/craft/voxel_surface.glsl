#ifndef CRAFT_VOXEL_SURFACE_GLSL
#define CRAFT_VOXEL_SURFACE_GLSL

#include "craft/shading_models.glsl"

// Voxel GBuffer channels beyond the engine layout:
// RT2 r metallic (0), g emission, b block light, a reserved;
// RT3 z sky light (phase 8 fills it), w shading model
// Sky light written until per-voxel sky light exists
#define CRAFT_SKY_LIGHT_OPEN 1.0
// No block emits light and no torch light exists yet
#define CRAFT_NO_EMISSION 0.0
#define CRAFT_NO_BLOCK_LIGHT 0.0
// RT2 alpha is unused by voxels
#define CRAFT_RT2_RESERVED 0.0

/// RT2 of a voxel texel.
vec4 CraftEncodeVoxelRT2(float metallic, float emission, float blockLight) {
    return vec4(metallic, clamp(emission, 0.0, 1.0),
                clamp(blockLight, 0.0, 1.0), CRAFT_RT2_RESERVED);
}

/// RT3 of a voxel texel: no object motion yet.
vec4 CraftEncodeVoxelRT3(float skyLight, uint shadingModel) {
    return SleakEncodeMotionModel(vec2(0.0), clamp(skyLight, 0.0, 1.0),
                                  shadingModel);
}

/// Emission strength of an atlas texel; a hook for emissive blocks.
float CraftTexelEmission(vec3 albedo) { return CRAFT_NO_EMISSION; }

/// Emission strength stored in a voxel texel's RT2.
float CraftVoxelEmission(vec4 rt2) { return clamp(rt2.g, 0.0, 1.0); }

/// Block light stored in a voxel texel's RT2.
float CraftVoxelBlockLight(vec4 rt2) { return clamp(rt2.b, 0.0, 1.0); }

/// Sky light stored in a voxel texel's RT3.
float CraftVoxelSkyLight(vec4 rt3) { return clamp(rt3.z, 0.0, 1.0); }

#endif
