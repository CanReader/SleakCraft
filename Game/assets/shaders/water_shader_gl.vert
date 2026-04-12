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

layout(std140, binding = 1) uniform MaterialUBO {
    uint  HasDiffuseMap, HasNormalMap, HasSpecularMap, HasRoughnessMap;
    uint  HasMetallicMap, HasAOMap, HasEmissiveMap, _matPad0;
    vec4  MatDiffuseColor;
    vec3  MatSpecularColor; float MatShininess;
    vec3  MatEmissiveColor; float MatEmissiveIntensity;
    float MatMetallic, MatRoughness, MatAO, MatNormalIntensity;
    vec2  MatTiling; vec2 MatOffset;
    float MatOpacity; float MatAlphaCutoff; float _matPad1, _matPad2;
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

out vec3  fragWorldPos;
out vec3  fragNormal;
out vec4  fragColor;
out vec4  fragShadowCoord;
out float fragTime;

void main() {
    // No vertex displacement — waves are purely fragment-shader normal maps.
    gl_Position  = WVP * vec4(inPosition, 1.0);

    fragWorldPos  = inPosition;
    fragNormal    = inNormal;
    fragColor     = inColor;
    fragTime      = MatTiling.x;     // game time from SetTiling()
    fragShadowCoord = ShadowLightVP * vec4(inPosition, 1.0);
}
