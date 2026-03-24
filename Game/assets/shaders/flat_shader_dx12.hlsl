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
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float3 WorldTan  : TEXCOORD2;
    float3 WorldBit  : TEXCOORD3;
    float4 Color     : COLOR;
    float2 TexCoord  : TEXCOORD4;
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

// ============================================================
// ACES film tone mapping (Stephen Hill fit)
// ============================================================
float3 ACESFilm(float3 x) {
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
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
    float3 diffuse  = lightColor * lightIntens * NdotL;

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
