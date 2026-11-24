#version 450
#extension GL_GOOGLE_include_directive : require

#include "craft/voxel_vertex.glsl"

layout(push_constant) uniform TransformPC {
    mat4 WVP;
    mat4 World;
};

#include "sleak/shadow_light.glsl"

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNorm;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out vec2 fragUV;
layout(location = 4) out vec4 fragShadowCoord;

void main() {
    vec3 inPosition = CraftVoxelPosition();
    vec3 inNormal = CraftVoxelNormal();
    vec4 inColor = CraftVoxelColor();
    vec2 inUV = CraftVoxelUV();

    gl_Position = WVP * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;  // Vulkan Y-axis flip

    vec4 worldPos = World * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 worldMat3 = mat3(World);
    fragWorldNorm = normalize(worldMat3 * inNormal);

    fragColor = inColor;
    fragUV    = inUV;

    // Shadow coordinate: offset along normal to reduce shadow acne,
    // then transform by light VP
    float normalBias = uLightDir.w;  // stored in LightDir.w
    vec4 biasedWorldPos = worldPos + vec4(fragWorldNorm * normalBias, 0.0);
    fragShadowCoord = uLightVP * biasedWorldPos;
}
