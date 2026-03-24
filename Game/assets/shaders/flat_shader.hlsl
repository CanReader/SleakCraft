// ============================================================
// Flat Material Shader (DirectX 11 HLSL)
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

// --- Constant Buffers ---

cbuffer TransformCB : register(b0) {
    row_major float4x4 WVP;
    row_major float4x4 World;
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

// --- Textures & Samplers ---

Texture2D diffuseTexture : register(t0);
SamplerState mainSampler : register(s0);

// ============================================================
// ACES film tone mapping (Stephen Hill fit)
// ============================================================
float3 ACESFilm(float3 x) {
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

// ============================================================
// UE4-style distance attenuation
// ============================================================
float AttenuateUE4(float distance, float range) {
    if (range <= 0.0) return 1.0;
    float d = distance / range;
    float d2 = d * d;
    float d4 = d2 * d2;
    float falloff = saturate(1.0 - d4);
    return (falloff * falloff) / (distance * distance + 1.0);
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

    output.WorldNorm = normalize(mul(input.NORMAL,   (float3x3)World));
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
    float ao = input.Color.r;
    float3 baseColor = texColor.rgb;

    float3 N = normalize(input.WorldNorm);

    // ---- Hemisphere Ambient (sky = top, ground = bottom) ----
    float3 skyAmbient    = AmbientColor * AmbientIntensity;
    float3 groundAmbient = skyAmbient * float3(0.55, 0.50, 0.45);
    float hemisphere     = N.y * 0.5 + 0.5;
    float3 ambient       = lerp(groundAmbient, skyAmbient, hemisphere);

    // ---- Accumulate Diffuse ----
    float3 diffuse = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < NumActiveLights; i++) {
        LightData light = Lights[i];

        float3 L;
        float attenuation = 1.0;

        if (light.Type == 0) {
            // Directional — wrap diffuse: softens terminator, side faces get ~15% sun
            L = normalize(-light.Direction);
            float NdotL = saturate(dot(N, L) * 0.85 + 0.15);
            diffuse += light.Color * light.Intensity * NdotL;
            continue;
        }
        else if (light.Type == 1) {
            float3 toLight = light.Position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = AttenuateUE4(dist, light.Range);
        }
        else if (light.Type == 2) {
            float3 toLight = light.Position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = AttenuateUE4(dist, light.Range);
            float theta   = dot(L, normalize(-light.Direction));
            float epsilon = light.SpotInnerCos - light.SpotOuterCos;
            float spotFactor = saturate((theta - light.SpotOuterCos) / max(epsilon, 0.0001));
            attenuation *= spotFactor;
        }
        else {
            float3 toLight = light.Position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = AttenuateUE4(dist, light.Range);
        }

        float NdotL = max(dot(N, L), 0.0);
        diffuse += light.Color * light.Intensity * attenuation * NdotL;
    }

    // ---- Compose: AO on ambient only, diffuse unoccluded ----
    float3 lit = baseColor * (ao * ambient + diffuse);

    // ---- ACES tone mapping ----
    lit = ACESFilm(lit);

    // ---- Distance fog ----
    if (FogEnd > 0.0) {
        float dist = length(input.WorldPos - CameraPos);
        float fogFactor = saturate((FogEnd - dist) / (FogEnd - FogStart));
        lit = lerp(FogColor.rgb, lit, fogFactor);
    }

    return float4(lit, texColor.a);
}
