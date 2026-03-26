// ============================================================
// Water Shader (DirectX 11 HLSL)
// Gerstner waves, Fresnel, GGX specular, volumetric scattering
// Analytical sky reflection, caustics, foam
// ============================================================

struct VS_INPUT
{
    float3 POSITION : POSITION;
    float3 NORMAL   : NORMAL;
    float4 TANGENT  : TANGENT;
    float4 COLOR    : COLOR;
    float2 TEXCOORD : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 Position    : SV_POSITION;
    float3 WorldPos    : TEXCOORD0;
    float3 WorldNorm   : TEXCOORD1;
    float3 WorldTan    : TEXCOORD2;
    float3 WorldBit    : TEXCOORD3;
    float4 Color       : COLOR;
    float2 TexCoord    : TEXCOORD4;
    float4 ShadowCoord : TEXCOORD5;
};

cbuffer TransformCB : register(b0) {
    row_major float4x4 WVP;
    row_major float4x4 World;
};

cbuffer MaterialCB : register(b1) {
    uint   HasDiffuseMap, HasNormalMap, HasSpecularMap, HasRoughnessMap;
    uint   HasMetallicMap, HasAOMap, HasEmissiveMap, _matPad0;
    float4 MatDiffuseColor;
    float3 MatSpecularColor; float MatShininess;
    float3 MatEmissiveColor; float MatEmissiveIntensity;
    float  MatMetallic, MatRoughness, MatAO, MatNormalIntensity;
    float2 MatTiling; float2 MatOffset;
    float  MatOpacity; float MatAlphaCutoff; float _matPad1, _matPad2;
};

struct LightData {
    float3 Position;  uint Type;
    float3 Direction; float Intensity;
    float3 Color;     float Range;
    float SpotInnerCos; float SpotOuterCos;
    float AreaWidth; float AreaHeight;
};

cbuffer LightCB : register(b2) {
    float3 CameraPos;
    uint NumActiveLights;
    float3 AmbientColor;
    float AmbientIntensity;
    float4 FogColor;
    float FogStart;
    float FogEnd;
    float2 _reserved;
    LightData Lights[16];
};

cbuffer ShadowCB : register(b5) {
    row_major float4x4 ShadowLightVP;
    float ShadowBias;
    float ShadowStrength;
    float ShadowTexelSize;
    float ShadowLightSize;
    uint  PCSSEnabled;
    uint  ShadowMapEnabled;
    float _shadowPad0, _shadowPad1;
};

Texture2D diffuseTexture : register(t0);
SamplerState mainSampler : register(s0);
Texture2D shadowMap : register(t3);
SamplerComparisonState shadowSampler : register(s3);

static const float SEA_LEVEL = 64.875;
static const float F0_WATER = 0.02;
static const float PI = 3.14159265;

// ============================================================
// Gerstner Wave
// ============================================================
struct GerstnerWave {
    float2 direction;
    float amplitude;
    float frequency;
    float speed;
    float steepness;
};

void GerstnerDisplacement(float3 pos, float time, out float3 displacement, out float3 normal) {
    GerstnerWave waves[6];
    waves[0].direction = normalize(float2(1.0, 0.3));  waves[0].amplitude = 0.08; waves[0].frequency = 0.8;  waves[0].speed = 1.2; waves[0].steepness = 0.4;
    waves[1].direction = normalize(float2(0.5, 1.0));  waves[1].amplitude = 0.05; waves[1].frequency = 1.5;  waves[1].speed = 1.8; waves[1].steepness = 0.35;
    waves[2].direction = normalize(float2(-0.3, 0.7)); waves[2].amplitude = 0.03; waves[2].frequency = 2.5;  waves[2].speed = 2.5; waves[2].steepness = 0.3;
    waves[3].direction = normalize(float2(0.8, -0.6)); waves[3].amplitude = 0.06; waves[3].frequency = 0.6;  waves[3].speed = 0.9; waves[3].steepness = 0.45;
    waves[4].direction = normalize(float2(-0.5, -0.8));waves[4].amplitude = 0.02; waves[4].frequency = 3.5;  waves[4].speed = 3.2; waves[4].steepness = 0.25;
    waves[5].direction = normalize(float2(0.2, -1.0)); waves[5].amplitude = 0.04; waves[5].frequency = 1.2;  waves[5].speed = 1.5; waves[5].steepness = 0.35;

    displacement = float3(0, 0, 0);
    normal = float3(0, 1, 0);

    for (int i = 0; i < 6; i++) {
        float A = waves[i].amplitude;
        float w = waves[i].frequency;
        float phi = waves[i].speed * w;
        float Q = waves[i].steepness / (w * A * 6.0);
        float2 D = waves[i].direction;
        float dotDP = dot(D, pos.xz);
        float phase = w * dotDP + phi * time;
        float S = sin(phase);
        float C = cos(phase);
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

// ============================================================
// Shadow
// ============================================================
static const float2 poissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790)
};

float CalcShadowPCF(float4 shadowCoord) {
    if (ShadowMapEnabled == 0) return 1.0;
    float3 projCoords = shadowCoord.xyz / shadowCoord.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) return 1.0;
    float2 fadeCoord = smoothstep(0.0, 0.05, projCoords.xy) * smoothstep(0.0, 0.05, 1.0 - projCoords.xy);
    float edgeFade = fadeCoord.x * fadeCoord.y;
    float shadow = 0.0;
    for (int i = 0; i < 16; i++)
        shadow += shadowMap.SampleCmpLevelZero(shadowSampler, projCoords.xy + poissonDisk[i] * ShadowTexelSize * 1.5, projCoords.z - ShadowBias);
    shadow /= 16.0;
    return lerp(1.0, shadow, ShadowStrength * edgeFade);
}

float3 ACESFilm(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float FresnelSchlick(float cosTheta) {
    return F0_WATER + (1.0 - F0_WATER) * pow(saturate(1.0 - cosTheta), 5.0);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float3 SampleSky(float3 dir, float3 sunDir, float3 sunColor) {
    float y = max(dir.y, 0.0);
    float3 sky = lerp(float3(0.7, 0.82, 0.95), float3(0.25, 0.45, 0.85), pow(y, 0.5));
    float sunDot = max(dot(dir, sunDir), 0.0);
    sky += sunColor * pow(sunDot, 512.0) * 3.0;
    sky += sunColor * pow(sunDot, 64.0) * 0.3;
    if (dir.y < 0.0) {
        float t = saturate(-dir.y * 3.0);
        sky = lerp(float3(0.35, 0.41, 0.47), float3(0.02, 0.05, 0.1), t);
    }
    return sky;
}

float3 WaterAbsorption(float depth) {
    return exp(-float3(0.45, 0.09, 0.04) * max(depth, 0.0));
}

float CausticPattern(float3 pos, float time) {
    float2 p = pos.xz * 0.8;
    float c = 0.0;
    for (int i = 0; i < 2; i++) {
        float t = time * (0.8 + float(i) * 0.4);
        float2 uv = p * (1.0 + float(i) * 0.5);
        c += sin(uv.x * 3.0 + t) * sin(uv.y * 3.0 - t * 0.7) * 0.5 + 0.5;
        c += sin(uv.x * 2.1 - t * 0.5 + uv.y * 1.7) * sin(uv.y * 2.8 + t * 0.3) * 0.5 + 0.5;
    }
    return saturate(c * 0.25);
}

float3 DetailNormal(float3 worldPos, float time) {
    float dx = cos(worldPos.x * 2.5 + worldPos.z * 0.7 + time * 1.3) * 0.15
             + cos(worldPos.x * 4.0 - worldPos.z * 1.2 + time * 2.1) * 0.08
             + cos(worldPos.x * 7.0 + worldPos.z * 2.3 + time * 3.5) * 0.04;
    float dz = cos(worldPos.z * 2.5 + worldPos.x * 0.5 + time * 1.1) * 0.15
             + cos(worldPos.z * 4.0 - worldPos.x * 0.9 + time * 1.7) * 0.08
             + cos(worldPos.z * 7.0 + worldPos.x * 1.8 + time * 2.9) * 0.04;
    return normalize(float3(-dx, 1.0, -dz));
}

// ============================================================
// Vertex Shader
// ============================================================
VS_OUTPUT VS_Main(VS_INPUT input) {
    VS_OUTPUT output;
    float time = World[0][3];
    float4 worldPos = mul(float4(input.POSITION, 1.0), World);

    float3 waveNormal = float3(0, 1, 0);
    float3 displaced = worldPos.xyz;
    if (input.NORMAL.y > 0.5) {
        float3 disp;
        GerstnerDisplacement(worldPos.xyz, time, disp, waveNormal);
        displaced += disp;
    }

    float3 offset = displaced - worldPos.xyz;
    output.Position = mul(float4(input.POSITION + offset, 1.0), WVP);
    output.WorldPos = displaced;
    output.WorldNorm = waveNormal;

    float3 up = abs(waveNormal.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    output.WorldTan = normalize(cross(up, waveNormal));
    output.WorldBit = cross(waveNormal, output.WorldTan);
    output.Color = input.COLOR;
    output.TexCoord = input.TEXCOORD;
    output.ShadowCoord = mul(float4(displaced, 1.0), ShadowLightVP);
    return output;
}

// ============================================================
// Pixel Shader
// ============================================================
float4 PS_Main(VS_OUTPUT input) : SV_Target
{
    float time = World[0][3];
    float3 N = normalize(input.WorldNorm);
    float3 V = normalize(CameraPos - input.WorldPos);

    if (N.y > 0.3) {
        float3 detail = DetailNormal(input.WorldPos, time);
        N = normalize(N + (detail - float3(0, 1, 0)) * 0.6);
    }

    // Find first directional light
    float3 sunDir = float3(0, -1, 0);
    float3 sunColor = float3(1, 1, 1);
    for (uint li = 0; li < NumActiveLights; li++) {
        if (Lights[li].Type == 0) {
            sunDir = normalize(-Lights[li].Direction);
            sunColor = Lights[li].Color * Lights[li].Intensity;
            break;
        }
    }

    float NdotV = max(dot(N, V), 0.001);
    float fresnel = FresnelSchlick(NdotV);

    float3 R = reflect(-V, N);
    float3 reflectionColor = SampleSky(R, sunDir, sunColor);

    float terrainDepth = max(SEA_LEVEL - input.WorldPos.y + 8.0, 0.0);
    float3 absorption = WaterAbsorption(terrainDepth);
    float depthFactor = saturate(terrainDepth / 20.0);
    float3 refractionColor = lerp(float3(0.05, 0.35, 0.4), float3(0.01, 0.05, 0.12), depthFactor) * absorption;

    float sssDot = max(dot(V, -sunDir), 0.0);
    float sss = pow(sssDot, 4.0) * (1.0 - NdotV) * 0.4;
    float3 sssColor = float3(0.1, 0.5, 0.4) * sunColor * sss;

    float caustic = CausticPattern(input.WorldPos, time);
    float3 causticColor = sunColor * caustic * 0.15 * (1.0 - depthFactor);

    float shadow = CalcShadowPCF(input.ShadowCoord);
    float rawNdotL = dot(N, sunDir);
    shadow *= smoothstep(-0.1, 0.2, rawNdotL);

    float NdotL = saturate(rawNdotL * 0.7 + 0.3);
    float3 ambient = AmbientColor * AmbientIntensity * 0.4;
    float3 diffuse = sunColor * NdotL * shadow;

    float3 H = normalize(V + sunDir);
    float NdotH = max(dot(N, H), 0.0);
    float D = DistributionGGX(NdotH, 0.05);
    float spec = D * fresnel * shadow;
    float3 specColor = sunColor * min(spec, 50.0);

    float3 waterBody = refractionColor * (ambient + diffuse) + causticColor * shadow + sssColor;
    float3 finalColor = lerp(waterBody, reflectionColor, fresnel);
    finalColor += specColor;

    float shoreDepth = saturate(terrainDepth / 3.0);
    float foam = 0.0;
    if (shoreDepth < 0.5) {
        float foamLine = sin(input.WorldPos.x * 3.0 + input.WorldPos.z * 2.0 + time * 2.0) * 0.5 + 0.5;
        foam = (1.0 - shoreDepth * 2.0) * foamLine * 0.6;
    }
    float crestFoam = saturate((input.WorldPos.y - SEA_LEVEL + 0.05) * 8.0);
    crestFoam *= crestFoam;
    foam = max(foam, crestFoam * 0.3);
    finalColor = lerp(finalColor, float3(0.9, 0.95, 1.0) * (ambient + diffuse), foam);

    finalColor = ACESFilm(finalColor);

    if (FogEnd > 0.0) {
        float dist = length(input.WorldPos - CameraPos);
        float fogFactor = saturate((FogEnd - dist) / (FogEnd - FogStart));
        finalColor = lerp(FogColor.rgb, finalColor, fogFactor);
    }

    float alpha = lerp(0.6, 0.95, fresnel);
    alpha = lerp(alpha, 0.85, depthFactor);
    alpha = max(alpha, foam * 0.8 + 0.2);

    return float4(finalColor, alpha);
}
