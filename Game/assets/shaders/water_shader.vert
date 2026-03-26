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

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNorm;
layout(location = 2) out vec3 fragWorldTan;
layout(location = 3) out vec3 fragWorldBit;
layout(location = 4) out vec4 fragColor;
layout(location = 5) out vec2 fragUV;
layout(location = 6) out vec4 fragShadowCoord;
layout(location = 7) out float fragTime;

// ============================================================
// Gerstner Wave
// ============================================================
struct GerstnerWave {
    vec2  direction;
    float amplitude;
    float frequency;
    float speed;
    float steepness;
};

vec3 GerstnerDisplacement(vec3 pos, float time, out vec3 normal) {
    // 6 overlapping Gerstner waves for realistic ocean surface
    GerstnerWave waves[6];
    waves[0] = GerstnerWave(normalize(vec2(1.0, 0.3)),  0.08, 0.8,  1.2, 0.4);
    waves[1] = GerstnerWave(normalize(vec2(0.5, 1.0)),  0.05, 1.5,  1.8, 0.35);
    waves[2] = GerstnerWave(normalize(vec2(-0.3, 0.7)), 0.03, 2.5,  2.5, 0.3);
    waves[3] = GerstnerWave(normalize(vec2(0.8, -0.6)), 0.06, 0.6,  0.9, 0.45);
    waves[4] = GerstnerWave(normalize(vec2(-0.5, -0.8)),0.02, 3.5,  3.2, 0.25);
    waves[5] = GerstnerWave(normalize(vec2(0.2, -1.0)), 0.04, 1.2,  1.5, 0.35);

    vec3 displacement = vec3(0.0);
    vec3 N = vec3(0.0, 1.0, 0.0);
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);

    for (int i = 0; i < 6; i++) {
        float A = waves[i].amplitude;
        float w = waves[i].frequency;
        float phi = waves[i].speed * w;
        float Q = waves[i].steepness / (w * A * 6.0);
        vec2  D = waves[i].direction;

        float dotDP = dot(D, pos.xz);
        float phase = w * dotDP + phi * time;
        float S = sin(phase);
        float C = cos(phase);

        displacement.x += Q * A * D.x * C;
        displacement.z += Q * A * D.y * C;
        displacement.y += A * S;

        // Accumulate normal/tangent/binormal
        float WA = w * A;
        tangent.x  -= Q * D.x * D.x * WA * S;
        tangent.y  += D.x * WA * C;
        tangent.z  -= Q * D.x * D.y * WA * S;

        binormal.x -= Q * D.x * D.y * WA * S;
        binormal.y += D.y * WA * C;
        binormal.z -= Q * D.y * D.y * WA * S;

        N.x -= D.x * WA * C;
        N.z -= D.y * WA * C;
        N.y -= Q * WA * S;
    }

    normal = normalize(N);
    return displacement;
}

void main() {
    float time = World[0][3];  // Time packed into World matrix by MeshBatch
    vec3 worldPos3 = inPosition;  // Vertices already in world space (World = identity)

    // Only displace top-facing water surfaces
    vec3 waveNormal = vec3(0.0, 1.0, 0.0);
    vec3 displaced = worldPos3;
    if (inNormal.y > 0.5) {
        vec3 disp = GerstnerDisplacement(worldPos3, time, waveNormal);
        displaced += disp;
    }

    // Re-project displaced position
    gl_Position = WVP * vec4(inPosition + (displaced - worldPos3), 1.0);
    gl_Position.y = -gl_Position.y;

    fragWorldPos = displaced;
    fragWorldNorm = waveNormal;

    // Compute tangent/bitangent from wave normal
    vec3 up = abs(waveNormal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    fragWorldTan = normalize(cross(up, waveNormal));
    fragWorldBit = cross(waveNormal, fragWorldTan);

    fragColor = inColor;
    fragUV = inUV;
    fragTime = time;

    float normalBias = uLightDir.w;
    vec4 biasedWorldPos = vec4(displaced, 1.0) + vec4(waveNormal * normalBias, 0.0);
    fragShadowCoord = uLightVP * biasedWorldPos;
}
