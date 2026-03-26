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

out vec3 fragWorldPos;
out vec3 fragWorldNorm;
out vec4 fragColor;
out vec2 fragUV;
out vec4 fragShadowCoord;
out float fragTime;

struct GerstnerWave {
    vec2  direction;
    float amplitude, frequency, speed, steepness;
};

void GerstnerDisplacement(vec3 pos, float time, out vec3 displacement, out vec3 normal) {
    GerstnerWave waves[6];
    waves[0] = GerstnerWave(normalize(vec2(1.0, 0.3)),  0.08, 0.8,  1.2, 0.4);
    waves[1] = GerstnerWave(normalize(vec2(0.5, 1.0)),  0.05, 1.5,  1.8, 0.35);
    waves[2] = GerstnerWave(normalize(vec2(-0.3, 0.7)), 0.03, 2.5,  2.5, 0.3);
    waves[3] = GerstnerWave(normalize(vec2(0.8, -0.6)), 0.06, 0.6,  0.9, 0.45);
    waves[4] = GerstnerWave(normalize(vec2(-0.5, -0.8)),0.02, 3.5,  3.2, 0.25);
    waves[5] = GerstnerWave(normalize(vec2(0.2, -1.0)), 0.04, 1.2,  1.5, 0.35);
    displacement = vec3(0.0);
    normal = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < 6; i++) {
        float A = waves[i].amplitude, w = waves[i].frequency;
        float phi = waves[i].speed * w;
        float Q = waves[i].steepness / (w * A * 6.0);
        vec2 D = waves[i].direction;
        float phase = w * dot(D, pos.xz) + phi * time;
        float S = sin(phase), C = cos(phase);
        displacement.x += Q * A * D.x * C;
        displacement.z += Q * A * D.y * C;
        displacement.y += A * S;
        float WA = w * A;
        normal.x -= D.x * WA * C;
        normal.z -= D.y * WA * C;
        normal.y -= Q * WA * S;
    }
    normal = normalize(normal);
}

void main() {
    float time = World[0][3];  // Time packed into World matrix by MeshBatch
    vec4 worldPos = vec4(inPosition, 1.0);  // Vertices already in world space

    vec3 waveNormal = vec3(0.0, 1.0, 0.0);
    vec3 displaced = worldPos.xyz;
    if (inNormal.y > 0.5) {
        vec3 disp;
        GerstnerDisplacement(worldPos.xyz, time, disp, waveNormal);
        displaced += disp;
    }

    vec3 offset = displaced - worldPos.xyz;
    gl_Position = WVP * vec4(inPosition + offset, 1.0);
    fragWorldPos = displaced;
    fragWorldNorm = waveNormal;
    fragColor = inColor;
    fragUV = inUV;
    fragTime = time;
    fragShadowCoord = ShadowLightVP * vec4(displaced, 1.0);
}
