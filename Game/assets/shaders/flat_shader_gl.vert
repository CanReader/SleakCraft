#version 450 core
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUV;

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
    gl_Position = SleakFrameGuardClip(WVP * vec4(inPosition, 1.0));

    vec4 worldPos = World * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 worldMat3 = mat3(World);
    fragWorldNorm = normalize(worldMat3 * inNormal);

    fragColor = inColor;
    fragUV    = inUV;

    fragShadowCoord = uLightVP * worldPos;
}
