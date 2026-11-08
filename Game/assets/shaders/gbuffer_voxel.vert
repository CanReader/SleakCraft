#version 450
#extension GL_GOOGLE_include_directive : require

// ============================================================
// GBuffer Geometry Pass - Voxel Vertex Shader
// Compact VoxelVertex layout: position, normal, color, UV only
// (no tangent, no bone data)
// ============================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUV;

// Transform push constant (same as default_shader.vert)
layout(push_constant) uniform TransformPC {
    mat4 WVP;
    mat4 World;
};

#include "sleak/shadow_light.glsl"
#include "sleak/frame.glsl"

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNorm;
layout(location = 2) out vec3 fragWorldTan;
layout(location = 3) out vec3 fragWorldBit;
layout(location = 4) out vec4 fragColor;
layout(location = 5) out vec2 fragUV;
layout(location = 6) out vec4 fragShadowCoord;

void main() {
    gl_Position = WVP * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;  // Vulkan Y-axis flip
    gl_Position = SleakFrameGuardClip(gl_Position);

    vec4 worldPos = World * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 worldMat3 = mat3(World);
    fragWorldNorm = normalize(worldMat3 * inNormal);
    fragWorldTan  = vec3(0.0);  // no tangent for voxels
    fragWorldBit  = vec3(0.0);  // no bitangent for voxels

    fragColor = inColor;
    fragUV    = inUV;

    float normalBias = uLightDir.w;
    vec4 biasedWorldPos = worldPos + vec4(fragWorldNorm * normalBias, 0.0);
    fragShadowCoord = uLightVP * biasedWorldPos;
}
