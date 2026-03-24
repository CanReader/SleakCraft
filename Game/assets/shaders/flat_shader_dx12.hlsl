// ============================================================
// Flat Material Shader (DirectX 12 HLSL)
// Hemisphere ambient + wrap Lambert diffuse
// AO applied only to ambient (correct: direct light ignores occlusion)
// ACES film tone mapping
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

// Transform CB — root parameter 0, register(b0)
cbuffer TransformCB : register(b0) {
    row_major float4x4 WVP;
    row_major float4x4 World;
};

// Light/fog CB — root parameter 3, register(b2)
cbuffer LightCB : register(b2) {
    float4 LightDir;        // xyz = direction, w = shadow normal bias
    float4 LightColor;      // rgb = color, a = intensity
    float4 Ambient;         // rgb = color, a = intensity
    float4 CameraPos;
    row_major float4x4 LightVP;
    float ShadowBias;
    float ShadowStrength;
    float ShadowTexelSize;
    float LightSize;
    float4 FogColor;
    float FogStart;
    float FogEnd;
    float2 _fogPad;
};

Texture2D diffuseTexture : register(t0);
SamplerState mainSampler : register(s0);

Texture2D shadowMapTex : register(t3);
SamplerComparisonState shadowSampler : register(s3);

// ============================================================
// ACES film tone mapping (Stephen Hill fit)
// ============================================================
float3 ACESFilm(float3 x) {
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

// ============================================================
// PCF Shadow with 16-sample Poisson Disk
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
    float3 projCoords = shadowCoord.xyz / shadowCoord.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y; // D3D UV flip

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    float2 fadeCoord = smoothstep(0.0, 0.05, projCoords.xy)
                     * smoothstep(0.0, 0.05, 1.0 - projCoords.xy);
    float edgeFade = fadeCoord.x * fadeCoord.y;

    float texelSize = ShadowTexelSize;
    float radius = 1.5;
    float shadow = 0.0;

    for (int i = 0; i < 16; i++) {
        shadow += shadowMapTex.SampleCmpLevelZero(shadowSampler,
            projCoords.xy + poissonDisk[i] * texelSize * radius,
            projCoords.z - ShadowBias);
    }
    shadow /= 16.0;

    shadow = lerp(1.0, shadow, ShadowStrength * edgeFade);
    return shadow;
}

// ============================================================
// Vertex Shader
// ============================================================
VS_OUTPUT VS_Main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.Position = mul(float4(input.POSITION, 1.0), WVP);

    float4 worldPos = mul(float4(input.POSITION, 1.0), World);
    output.WorldPos = worldPos.xyz;

    output.WorldNorm = normalize(mul(input.NORMAL,      (float3x3)World));
    output.WorldTan  = normalize(mul(input.TANGENT.xyz, (float3x3)World));
    output.WorldBit  = cross(output.WorldNorm, output.WorldTan) * input.TANGENT.w;

    output.Color    = input.COLOR;
    output.TexCoord = input.TEXCOORD;

    output.ShadowCoord = mul(worldPos, LightVP);

    return output;
}

// ============================================================
// Pixel Shader
// ============================================================
float4 PS_Main(VS_OUTPUT input) : SV_Target
{
    float4 texColor = diffuseTexture.Sample(mainSampler, input.TexCoord);
    if (texColor.a < 0.5)
        discard;

    // AO is stored in vertex color (r=g=b=ao). Apply only to ambient.
    float ao        = input.Color.r;
    float3 baseColor = texColor.rgb;

    float3 N            = normalize(input.WorldNorm);
    float3 lightDir     = normalize(LightDir.xyz);
    float3 lightColor   = LightColor.rgb;
    float  lightIntens  = LightColor.a;

    // ---- Hemisphere Ambient ----
    float3 skyAmbient    = Ambient.rgb * Ambient.a;
    float3 groundAmbient = skyAmbient * float3(0.55, 0.50, 0.45);
    float  hemisphere    = N.y * 0.5 + 0.5;
    float3 ambient       = lerp(groundAmbient, skyAmbient, hemisphere);

    // ---- Wrap diffuse: softens shadow terminator ----
    float NdotL     = saturate(dot(N, -lightDir) * 0.85 + 0.15);
    float shadow    = CalcShadowPCF(input.ShadowCoord);
    float3 diffuse  = lightColor * lightIntens * NdotL * shadow;

    // ---- Compose: AO on ambient only ----
    float3 lit = baseColor * (ao * ambient + diffuse);

    // ---- ACES tone mapping ----
    lit = ACESFilm(lit);

    // ---- Distance fog ----
    if (FogEnd > 0.0) {
        float dist = length(input.WorldPos - CameraPos.xyz);
        float fogFactor = saturate((FogEnd - dist) / (FogEnd - FogStart));
        lit = lerp(FogColor.rgb, lit, fogFactor);
    }

    return float4(lit, texColor.a);
}
