#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec2 inUV;

layout(std140, binding = 0) uniform TransformUBO {
    mat4 WVP;
    mat4 World;
};

out vec3 fragWorldPos;
out vec3 fragWorldNorm;
out vec4 fragColor;
out vec2 fragUV;

void main() {
    gl_Position = WVP * vec4(inPosition, 1.0);

    vec4 worldPos = World * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 worldMat3 = mat3(World);
    fragWorldNorm = normalize(worldMat3 * inNormal);

    fragColor = inColor;
    fragUV    = inUV;
}
