#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec2 inUV;

layout(push_constant) uniform TransformPC {
    mat4 WVP;
    mat4 World;
};

layout(set = 1, binding = 0) uniform MaterialUBO {
    uint  hasDiffuseMap, hasNormalMap, hasSpecularMap, hasRoughnessMap;
    uint  hasMetallicMap, hasAOMap, hasEmissiveMap, _matPad0;
    vec4  matDiffuseColor;
    vec3  matSpecularColor; float matShininess;
    vec3  matEmissiveColor; float matEmissiveIntensity;
    float matMetallic, matRoughness, matAO, matNormalIntensity;
    vec2  matTiling; vec2 matOffset;
    float matOpacity; float matAlphaCutoff; float _matPad1, _matPad2;
};

layout(set = 2, binding = 0) uniform ShadowLightUBO {
    vec4  uLightDir;
    vec4  uLightColor;
    vec4  uAmbient;
    vec4  uCameraPos;
    mat4  uLightVP;
    float uShadowBias;
    float uShadowStrength;
    float uShadowTexelSize;
    float uLightSize;
    vec4  uFogColor;
    float uFogStart;
    float uFogEnd;
    float _fogPad[2];
};

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec4  fragColor;
layout(location = 3) out vec4  fragShadowCoord;
layout(location = 4) out float fragTime;

void main() {
    // No vertex displacement — wave motion is done entirely in the fragment shader
    // via procedural normal mapping (BSL approach).
    gl_Position   = WVP * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;   // Vulkan NDC y-flip

    fragWorldPos   = inPosition;
    fragNormal     = inNormal;
    fragColor      = inColor;
    fragTime       = matTiling.x;     // game time packed here by SetTiling()

    float normalBias = uLightDir.w;
    fragShadowCoord  = uLightVP * (vec4(inPosition, 1.0) + vec4(inNormal * normalBias, 0.0));
}
