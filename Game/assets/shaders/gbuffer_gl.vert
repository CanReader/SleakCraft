#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUV;

layout(std140, binding = 0) uniform TransformUBO {
    mat4 WVP;
    mat4 World;
};

layout(std140, binding = 5) uniform ShadowUBO {
    mat4  ShadowLightVP;
    float ShadowBias;
    float ShadowStrength;
    float ShadowTexelSize;
    float ShadowLightSize;
    uint  PCSSEnabled;
    uint  ShadowMapEnabled;
    float _shadowPad0, _shadowPad1;
};

out vec3 fragWorldPos;
out vec3 fragWorldNorm;
out vec3 fragWorldTan;
out vec3 fragWorldBit;
out vec4 fragColor;
out vec2 fragUV;
out vec4 fragShadowCoord;

void main() {
    gl_Position = WVP * vec4(inPosition, 1.0);

    vec4 worldPos  = World * vec4(inPosition, 1.0);
    fragWorldPos   = worldPos.xyz;

    mat3 normalMat = transpose(inverse(mat3(World)));
    vec3 N = normalize(normalMat * inNormal);

    // Compute tangent from normal for axis-aligned voxel faces
    vec3 T = abs(N.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    fragWorldNorm  = N;
    fragWorldTan   = T;
    fragWorldBit   = B;
    fragColor      = inColor;
    fragUV         = inUV;
    fragShadowCoord = ShadowLightVP * worldPos;
}
