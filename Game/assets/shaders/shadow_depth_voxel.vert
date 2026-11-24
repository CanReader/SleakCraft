#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow Depth Pass - Voxel Vertex Only
// Packed VoxelVertex: only position needed for shadow map

#include "craft/voxel_vertex.glsl"
#include "sleak/frame.glsl"

// Push constant: LightVP*World in slot 0, World in slot 1
layout(push_constant) uniform TransformPC {
    mat4 WVP;
    mat4 World;
};

void main() {
    gl_Position = SleakFrameGuardClip(WVP * vec4(CraftVoxelPosition(), 1.0));
}
