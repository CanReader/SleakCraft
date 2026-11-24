#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "craft/voxel_vertex.glsl"

layout(std140, binding = 0) uniform TransformUBO {
    mat4 WVP;
    mat4 World;
};

#include "sleak/shadow_light.glsl"
#include "sleak/frame.glsl"

out vec3 fragWorldPos;
out vec3 fragWorldNorm;
out vec4 fragColor;
out vec2 fragUV;
out vec4 fragShadowCoord;

void main() {
    vec3 inPosition = CraftVoxelPosition();
    vec3 inNormal = CraftVoxelNormal();
    vec4 inColor = CraftVoxelColor();
    vec2 inUV = CraftVoxelUV();

    gl_Position = SleakFrameGuardClip(WVP * vec4(inPosition, 1.0));

    vec4 worldPos = World * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 worldMat3 = mat3(World);
    fragWorldNorm = normalize(worldMat3 * inNormal);

    fragColor = inColor;
    fragUV    = inUV;

    fragShadowCoord = uLightVP * worldPos;
}
