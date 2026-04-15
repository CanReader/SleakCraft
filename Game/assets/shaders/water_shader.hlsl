// Water Shader — DirectX 11 HLSL
// BSL-style: no vertex displacement, procedural fragment normals, BSL water color.

struct VS_INPUT  { float3 POSITION:POSITION; float3 NORMAL:NORMAL; float4 COLOR:COLOR; float2 TEXCOORD:TEXCOORD; };
struct VS_OUTPUT {
    float4 Position    : SV_POSITION;
    float3 WorldPos    : TEXCOORD0;
    float3 Normal      : TEXCOORD1;
    float4 Color       : COLOR;
    float4 ShadowCoord : TEXCOORD2;
    float  Time        : TEXCOORD3;
};

cbuffer TransformCB : register(b0) { row_major float4x4 WVP; row_major float4x4 World; };

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

struct LightData { float3 Position; uint Type; float3 Direction; float Intensity; float3 Color; float Range; float SpotInnerCos; float SpotOuterCos; float AreaWidth; float AreaHeight; };
cbuffer LightCB : register(b2) {
    float3    CameraPos; uint NumActiveLights;
    float3    AmbientColor; float AmbientIntensity;
    float4    FogColor; float FogStart; float FogEnd; float2 _reserved;  // FogColor = horizon
    LightData Lights[16];
    // Trailing block — must match LightCBData order in ConstantBuffer.hpp.
    float4    FogColorZenith;       // zenith
    float     HeightFogTop;
    float     HeightFogDensity;
    float     HeightFogFalloff;
    float     HeightFogEnabled;
};

cbuffer ShadowCB : register(b5) {
    row_major float4x4 ShadowLightVP;
    float ShadowBias; float ShadowStrength; float ShadowTexelSize; float ShadowLightSize;
    uint  PCSSEnabled; uint ShadowMapEnabled; float _sp0, _sp1;
};

Texture2D diffuseTexture : register(t0); SamplerState mainSampler : register(s0);
Texture2D shadowMap      : register(t3); SamplerComparisonState shadowSampler : register(s3);

// ---- Shadow ----
static const float2 disk[16] = {
    float2(-0.9465,-0.1484), float2(-0.7431, 0.5353), float2(-0.5863,-0.5879), float2(-0.3935, 0.1025),
    float2(-0.2428, 0.7722), float2(-0.1074,-0.3075), float2( 0.0542,-0.8645), float2( 0.1267, 0.4300),
    float2( 0.2787,-0.1353), float2( 0.3842, 0.6501), float2( 0.4714,-0.5537), float2( 0.5765, 0.1675),
    float2( 0.6712,-0.3340), float2( 0.7527, 0.4813), float2( 0.8745,-0.0910), float2( 0.9601, 0.2637)
};
float IGN(float2 p) { float3 m=float3(0.06711056,0.00583715,52.9829189); return frac(m.z*frac(dot(p,m.xy))); }
float CalcShadow(float4 sc, float2 sp) {
    if (ShadowMapEnabled == 0) return 1.0;
    float3 p = sc.xyz / sc.w;
    p.xy = p.xy * 0.5 + 0.5; p.y = 1.0 - p.y;
    if (p.z > 1.0 || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) return 1.0;
    float2 fc = smoothstep(0.0, 0.05, p.xy) * smoothstep(0.0, 0.05, 1.0 - p.xy);
    float angle = IGN(sp) * 6.28318530;
    float sa = sin(angle), ca = cos(angle);
    float2x2 rot = float2x2(ca, sa, -sa, ca);
    float rad = ShadowTexelSize * ShadowLightSize * 6.0, s = 0.0;
    for (int i = 0; i < 16; i++) s += shadowMap.SampleCmpLevelZero(shadowSampler, p.xy + mul(rot, disk[i])*rad, p.z - ShadowBias);
    return lerp(1.0, s/16.0, ShadowStrength * fc.x * fc.y);
}

// ---- Wave height field ----
float WaterHeight(float2 xz, float time) {
    float2 w1 = float2( time*0.45,  time*0.30);
    float2 w2 = float2(-time*0.35,  time*0.50);
    float h = 0.0;
    h += sin(xz.x*0.24+w1.x) * sin(xz.y*0.19+w1.y);
    h += sin(xz.x*0.33+w2.x-xz.y*0.11) * 0.65;
    h += sin(xz.x*0.80+w1.x*1.9+xz.y*0.58) * 0.40;
    h += sin(xz.x*1.10-w2.x*2.1) * sin(xz.y*0.88+w2.y*1.4) * 0.35;
    h += sin(xz.x*2.20+w1.x*3.3) * sin(xz.y*1.85-w1.y*2.7) * 0.15;
    h += sin(xz.x*3.00-w2.x*4.2+xz.y*2.30) * 0.10;
    return h;
}
float3 WaveNormal(float3 wp, float3 origNorm, float time) {
    if (origNorm.y < 0.5) return origNorm;
    float d = 0.15;
    float xd = (WaterHeight(wp.xz - float2(d,0),time) - WaterHeight(wp.xz + float2(d,0),time)) / (2.0*d);
    float zd = (WaterHeight(wp.xz - float2(0,d),time) - WaterHeight(wp.xz + float2(0,d),time)) / (2.0*d);
    return normalize(float3(xd*0.30, 1.0, zd*0.30));
}

// ---- Sky ----
float3 SampleSky(float3 dir, float3 sunDir, float3 sunCol, float3 amb) {
    float y = max(dir.y, 0.0);
    float3 horiz  = lerp(float3(0.70,0.82,0.95), amb, 0.30);
    float3 zenith = lerp(float3(0.22,0.42,0.82), amb*0.65, 0.25);
    float3 sky = lerp(horiz, zenith, pow(y, 0.45));
    float sd = max(dot(dir,sunDir), 0.0);
    sky += sunCol*pow(sd,500.0)*5.0 + sunCol*pow(sd,60.0)*0.5;
    if (dir.y < 0.0) sky = lerp(horiz*0.5, float3(0.02,0.04,0.08), saturate(-dir.y*4.0));
    return sky;
}
float3 ACESFilm(float3 x) { return saturate((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14)); }
float  Fresnel(float c)   { return 0.02 + 0.98*pow(saturate(1.0-c),5.0); }
float  GGX(float NdotH, float r) { float a=r*r*r*r; float d=NdotH*NdotH*(a-1.0)+1.0; return a/(3.14159265*d*d); }

// Sky-matched gradient fog + exponential height fog (mirrors lighting_pass.hlsl/.frag).
float3 ApplyFog(float3 color, float3 worldPos) {
    if (FogEnd <= 0.0) return color;
    float3 toFrag = worldPos - CameraPos;
    float dist = length(toFrag.xz);
    float distFog = saturate((dist - FogStart) / max(FogEnd - FogStart, 1e-4));
    float heightFog = 0.0;
    if (HeightFogEnabled > 0.5) {
        float h = max(HeightFogTop - worldPos.y, 0.0);
        heightFog = saturate(HeightFogDensity * (1.0 - exp(-h * HeightFogFalloff)));
    }
    float fogAmount = 1.0 - (1.0 - distFog) * (1.0 - heightFog);
    float3 viewDir = normalize(toFrag);
    float t = smoothstep(0.0, 1.0, saturate(viewDir.y * 0.5 + 0.5));
    float3 fogColor = lerp(FogColor.rgb, FogColorZenith.rgb, t);
    return lerp(color, fogColor, fogAmount);
}

// ---- VS ----
VS_OUTPUT VS_Main(VS_INPUT i) {
    VS_OUTPUT o;
    float4 wp = mul(float4(i.POSITION, 1.0), World);
    o.Position    = mul(float4(i.POSITION, 1.0), WVP);
    o.WorldPos    = wp.xyz;
    o.Normal      = i.NORMAL;
    o.Color       = i.COLOR;
    o.ShadowCoord = mul(float4(wp.xyz, 1.0), ShadowLightVP);
    o.Time        = MatTiling.x;
    return o;
}

// ---- PS ----
float4 PS_Main(VS_OUTPUT i) : SV_Target {
    float time = i.Time;
    float3 N = WaveNormal(i.WorldPos, i.Normal, time);
    float3 V = normalize(CameraPos - i.WorldPos);

    float3 sunDir = float3(0,1,0), sunCol = float3(1,1,1);
    for (uint li = 0; li < NumActiveLights; li++) {
        if (Lights[li].Type == 0) { sunDir = normalize(-Lights[li].Direction); sunCol = Lights[li].Color * Lights[li].Intensity; break; }
    }
    float3 amb = AmbientColor * AmbientIntensity;

    float NdotV = max(dot(N,V), 0.001);
    float fresnel = Fresnel(NdotV);

    float shadow = CalcShadow(i.ShadowCoord, i.Position.xy);
    shadow *= smoothstep(-0.1, 0.2, dot(N,sunDir));
    float NdotL = saturate(dot(N,sunDir)*0.65+0.35);
    float3 diffuse = sunCol * NdotL * shadow;

    float3 wcSqrt = float3(64.0,160.0,255.0)/255.0 * 0.35;
    float3 wColor = wcSqrt * wcSqrt * 3.0;
    float3 waterBody = wColor * (amb*1.8 + diffuse*0.9);
    float sss = pow(max(dot(V,-sunDir),0.0),4.0) * (1.0-NdotV) * 0.35;
    waterBody += float3(0.0,0.08,0.14) * sunCol * sss;

    float3 R    = reflect(-V,N);
    float3 refl = SampleSky(R, sunDir, sunCol, amb);

    float NdotH = max(dot(N,normalize(V+sunDir)),0.0);
    float spec  = GGX(NdotH,0.07)*fresnel*shadow;
    float3 specC = sunCol * min(spec, 40.0);

    float3 final = lerp(waterBody, refl, fresnel) + specC;

    if (i.Normal.y > 0.5) {
        float2 cp = i.WorldPos.xz * 0.65; float t = time*0.9;
        float c = sin(cp.x*2.7+t)*sin(cp.y*2.7-t*0.75)*0.5+0.5;
        c += sin(cp.x*1.9-t*0.55+cp.y*1.6)*sin(cp.y*2.4+t*0.35)*0.5+0.5;
        c = saturate(pow(c*0.5,1.6)*1.5);
        final += sunCol * c * 0.05 * shadow;
    }

    final = ACESFilm(final);
    final = ApplyFog(final, i.WorldPos);

    float alpha = lerp(0.72, 0.97, pow(saturate(1.0-NdotV), 2.0));
    return float4(final, alpha);
}
