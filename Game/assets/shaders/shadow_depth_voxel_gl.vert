#version 450 core
#extension GL_GOOGLE_include_directive : require

// Shadow depth pass, OpenGL, voxel format: position only

#include "craft/voxel_vertex.glsl"

// LightVP * World in WVP during the shadow pass
layout(std140, binding = 0) uniform TransformUBO {
    mat4 WVP;
    mat4 World;
};

#include "sleak/frame.glsl"

void main() {
    gl_Position = SleakFrameGuardClip(WVP * vec4(CraftVoxelPosition(), 1.0));
}
