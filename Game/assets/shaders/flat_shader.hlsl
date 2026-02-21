// ============================================================
// Flat Material Shader (DirectX 11 HLSL)
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
    float4 _reserved[2];
    LightData Lights[16];
};

// --- Textures & Samplers ---

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
// Pixel Shader
// ============================================================
float4 PS_Main(VS_OUTPUT input) : SV_Target
{
    float4 texColor = diffuseTexture.Sample(mainSampler,
                                             input.TexCoord);
    float4 baseColor = texColor * input.Color;

    float3 N = normalize(input.WorldNorm);

    // ---- Hemisphere Ambient ----
    float3 ambientColor = AmbientColor * AmbientIntensity;
    float3 groundColor = ambientColor * float3(0.7, 0.65, 0.6);
    float hemisphere = N.y * 0.5 + 0.5;
    float3 ambient = lerp(groundColor, ambientColor, hemisphere);

    // ---- Accumulate Diffuse (Lambert only, no specular) ----
    float3 diffuse = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < NumActiveLights; i++) {
        LightData light = Lights[i];

        float3 L;
        float attenuation = 1.0;

        if (light.Type == 0) {
            // Directional light
            L = normalize(-light.Direction);
        }
        else if (light.Type == 1) {
            // Point light
            float3 toLight = light.Position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = AttenuateUE4(dist, light.Range);
        }
        else if (light.Type == 2) {
            // Spot light
            float3 toLight = light.Position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = AttenuateUE4(dist, light.Range);

            float theta = dot(L, normalize(-light.Direction));
            float epsilon = light.SpotInnerCos
                          - light.SpotOuterCos;
            float spotFactor = saturate(
                (theta - light.SpotOuterCos)
                / max(epsilon, 0.0001));
            attenuation *= spotFactor;
        }
        else {
            // Area light (representative point approx)
            float3 toLight = light.Position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = AttenuateUE4(dist, light.Range);
        }

        float NdotL = max(dot(N, L), 0.0);
        diffuse += light.Color * light.Intensity
                 * attenuation * NdotL;
    }

    // ---- Compose (no specular) ----
    float3 lit = baseColor.rgb * (ambient + diffuse);

    // ---- Tone mapping (Reinhard) ----
    lit = lit / (lit + float3(1.0, 1.0, 1.0));

    return float4(lit, baseColor.a);
}
