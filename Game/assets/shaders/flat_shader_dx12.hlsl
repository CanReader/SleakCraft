// ============================================================
// Flat Material Shader (DirectX 12 HLSL)
// Hardcoded directional light fallback
// (DX12 root signature does not yet support Material/Light CBs)
// Hemisphere ambient + Lambert diffuse, no specular
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

// Texture + sampler — root parameter 1, register(t0/s0)
Texture2D diffuseTexture : register(t0);
SamplerState mainSampler : register(s0);

// ============================================================
// Vertex Shader
// ============================================================
VS_OUTPUT VS_Main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.Position = mul(float4(input.POSITION, 1.0), WVP);

    float4 worldPos = mul(float4(input.POSITION, 1.0), World);
    output.WorldPos = worldPos.xyz;

    output.WorldNorm = normalize(mul(input.NORMAL,
                                     (float3x3) World));
    output.WorldTan = normalize(mul(input.TANGENT.xyz,
                                    (float3x3) World));
    output.WorldBit = cross(output.WorldNorm, output.WorldTan)
                      * input.TANGENT.w;

    output.Color    = input.COLOR;
    output.TexCoord = input.TEXCOORD;

    return output;
}

// ============================================================
// Pixel Shader
// ============================================================
float4 PS_Main(VS_OUTPUT input) : SV_Target
{
    float4 texColor = diffuseTexture.Sample(mainSampler,
                                             input.TexCoord);
    float4 baseColor = texColor * input.Color;

    // Hardcoded directional light (sun-like)
    float3 lightDir = normalize(float3(-0.3, -1.0, -0.4));
    float3 lightColor = float3(1.0, 0.95, 0.9);
    float lightIntensity = 1.2;

    float3 N = normalize(input.WorldNorm);

    // ---- Hemisphere Ambient ----
    float3 ambientColor = float3(0.15, 0.15, 0.18);
    float3 groundColor = ambientColor * float3(0.7, 0.65, 0.6);
    float hemisphere = N.y * 0.5 + 0.5;
    float3 ambient = lerp(groundColor, ambientColor, hemisphere);

    // ---- Diffuse (Lambert, no specular) ----
    float NdotL = max(dot(N, -lightDir), 0.0);
    float3 diffuse = lightColor * lightIntensity * NdotL;

    // ---- Compose (no specular) ----
    float3 lit = baseColor.rgb * (ambient + diffuse);

    // ---- Tone mapping (Reinhard) ----
    lit = lit / (lit + float3(1.0, 1.0, 1.0));

    return float4(lit, baseColor.a);
}
