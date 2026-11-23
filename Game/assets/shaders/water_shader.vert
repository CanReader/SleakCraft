#version 450
#extension GL_GOOGLE_include_directive : require

#include "craft/voxel_vertex.glsl"

layout(push_constant) uniform TransformPC {
    mat4 WVP;
    mat4 World;
};

#include "sleak/shadow_light.glsl"

// Scene read block (VertexLayoutDesc::transparentReadsScene)
layout(set = 3, binding = 5) uniform SceneReadUBO {
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uScreenSize;
    vec4 uSceneCamera;  // xy = TAA jitter in NDC, z = near, w = far
    vec4 uSceneFlags;
};

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out vec3  fragNormal;
layout(location = 3) out vec4  fragShadowCoord;
layout(location = 4) out float fragSky;  // sky light (inColor.g, phase 8)

void main() {
    vec3 inPosition = CraftVoxelPosition();
    vec3 inNormal = CraftVoxelNormal();
    vec4 inColor = CraftVoxelColor();

    // Waves are fragment normals only; the surface stays flat
    gl_Position   = WVP * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;   // Vulkan NDC y-flip
    // Same sub-pixel jitter as the opaque GBuffer geometry
    gl_Position.xy += uSceneCamera.xy * gl_Position.w;

    fragWorldPos   = inPosition;
    fragNormal     = inNormal;
    fragSky        = inColor.g;

    float normalBias = uLightDir.w;
    fragShadowCoord  = uLightVP * (vec4(inPosition, 1.0) + vec4(inNormal * normalBias, 0.0));
}
