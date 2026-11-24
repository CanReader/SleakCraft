#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "craft/voxel_vertex.glsl"

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

void main() {
    vec3 inPosition = CraftVoxelPosition();
    vec3 inNormal = CraftVoxelNormal();
    vec4 inColor = CraftVoxelColor();

    // Waves are fragment normals only; the surface stays flat
    gl_Position  = WVP * vec4(inPosition, 1.0);

    fragWorldPos  = inPosition;
    fragNormal    = inNormal;
    fragColor     = inColor;  // g: sky light (phase 8)
    fragShadowCoord = ShadowLightVP * vec4(inPosition, 1.0);
}
