#version 450 core
#extension GL_GOOGLE_include_directive : require

// Shadow depth pass, OpenGL, voxel format: position only

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUV;

// LightVP * World in WVP during the shadow pass
layout(std140, binding = 0) uniform TransformUBO {
    mat4 WVP;
    mat4 World;
};

#include "sleak/frame.glsl"

void main() {
    gl_Position = SleakFrameGuardClip(WVP * vec4(inPosition, 1.0));
}
