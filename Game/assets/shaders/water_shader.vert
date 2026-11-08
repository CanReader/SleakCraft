#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUV;

layout(push_constant) uniform TransformPC {
    mat4 WVP;
    mat4 World;
};

#include "sleak/shadow_light.glsl"
#include "sleak/frame.glsl"

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
layout(location = 4) out float fragTime;

void main() {
    // No vertex displacement — wave motion is done entirely in the fragment shader
    // via procedural normal mapping (BSL approach).
    gl_Position   = WVP * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;   // Vulkan NDC y-flip
    // Same sub-pixel jitter as the opaque GBuffer geometry
    gl_Position.xy += uSceneCamera.xy * gl_Position.w;

    fragWorldPos   = inPosition;
    fragNormal     = inNormal;
    fragTime       = uFrame.Time;

    float normalBias = uLightDir.w;
    fragShadowCoord  = uLightVP * (vec4(inPosition, 1.0) + vec4(inNormal * normalBias, 0.0));
}
