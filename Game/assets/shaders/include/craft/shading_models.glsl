#ifndef CRAFT_SHADING_MODELS_GLSL
#define CRAFT_SHADING_MODELS_GLSL

#include "sleak/gbuffer.glsl"

// SleakCraft shading model ids in GBuffer RT3.w; 0 belongs to the engine
#define CRAFT_SHADING_MODEL_VOXEL 1u
// Last id the voxel lighting shades (phase 11 appends foliage)
#define CRAFT_SHADING_MODEL_LAST_VOXEL CRAFT_SHADING_MODEL_VOXEL

/// True for ids the voxel lighting composition shades.
bool CraftIsVoxelModel(uint id) {
    return id >= CRAFT_SHADING_MODEL_VOXEL &&
           id <= CRAFT_SHADING_MODEL_LAST_VOXEL;
}

#endif
