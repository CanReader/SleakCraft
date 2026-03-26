#version 450 core

in vec3 fragWorldPos;
in vec3 fragWorldNorm;
in vec4 fragColor;
in vec2 fragUV;
in vec4 fragShadowCoord;
in float fragTime;

out vec4 outColor;

struct LightData {
    vec3 Position;  uint Type;
    vec3 Direction; float Intensity;
    vec3 Color;     float Range;
    float SpotInnerCos; float SpotOuterCos;
    float AreaWidth; float AreaHeight;
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

layout(std140, binding = 2) uniform LightUBO {
    vec3 CameraPos;
    uint NumActiveLights;
    vec3 AmbientColor;
    float AmbientIntensity;
    vec4 FogColor;
    float FogStart;
    float FogEnd;
    float _pad0, _pad1;
    LightData Lights[16];
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

layout(binding = 0) uniform sampler2D diffuseTexture;
layout(binding = 3) uniform sampler2DShadow shadowMap;

const float SEA_LEVEL = 64.875;
const float F0_WATER = 0.02;
const float PI = 3.14159265;

const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

float CalcShadowPCF(vec4 sc) {
    if (ShadowMapEnabled == 0u) return 1.0;
    vec3 p = sc.xyz / sc.w;
    p.xy = p.xy * 0.5 + 0.5;
    p.z = p.z * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    vec2 fc = smoothstep(vec2(0.0), vec2(0.05), p.xy) * smoothstep(vec2(0.0), vec2(0.05), vec2(1.0) - p.xy);
    float shadow = 0.0;
    for (int i = 0; i < 16; i++)
        shadow += texture(shadowMap, vec3(p.xy + poissonDisk[i] * ShadowTexelSize * 1.5, p.z - ShadowBias));
    shadow /= 16.0;
    return mix(1.0, shadow, ShadowStrength * fc.x * fc.y);
}

vec3 ACESFilm(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), vec3(0.0), vec3(1.0)); }
float FresnelSchlick(float c) { return F0_WATER + (1.0 - F0_WATER) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0); }
float DistributionGGX(float NdotH, float r) { float a2=r*r*r*r; float d=NdotH*NdotH*(a2-1.0)+1.0; return a2/(PI*d*d); }

vec3 SampleSky(vec3 dir, vec3 sunDir, vec3 sunColor) {
    float y = max(dir.y, 0.0);
    vec3 sky = mix(vec3(0.7, 0.82, 0.95), vec3(0.25, 0.45, 0.85), pow(y, 0.5));
    float sd = max(dot(dir, sunDir), 0.0);
    sky += sunColor * pow(sd, 512.0) * 3.0 + sunColor * pow(sd, 64.0) * 0.3;
    if (dir.y < 0.0) sky = mix(vec3(0.35, 0.41, 0.47), vec3(0.02, 0.05, 0.1), clamp(-dir.y * 3.0, 0.0, 1.0));
    return sky;
}

vec3 DetailNormal(vec3 wp, float t) {
    float dx = cos(wp.x*2.5+wp.z*0.7+t*1.3)*0.15 + cos(wp.x*4.0-wp.z*1.2+t*2.1)*0.08 + cos(wp.x*7.0+wp.z*2.3+t*3.5)*0.04;
    float dz = cos(wp.z*2.5+wp.x*0.5+t*1.1)*0.15 + cos(wp.z*4.0-wp.x*0.9+t*1.7)*0.08 + cos(wp.z*7.0+wp.x*1.8+t*2.9)*0.04;
    return normalize(vec3(-dx, 1.0, -dz));
}

float CausticPattern(vec3 pos, float time) {
    vec2 p = pos.xz * 0.8; float c = 0.0;
    for (int i = 0; i < 2; i++) {
        float t = time * (0.8 + float(i) * 0.4);
        vec2 uv = p * (1.0 + float(i) * 0.5);
        c += sin(uv.x*3.0+t)*sin(uv.y*3.0-t*0.7)*0.5+0.5;
        c += sin(uv.x*2.1-t*0.5+uv.y*1.7)*sin(uv.y*2.8+t*0.3)*0.5+0.5;
    }
    return clamp(c * 0.25, 0.0, 1.0);
}

void main() {
    float time = fragTime;
    vec3 N = normalize(fragWorldNorm);
    vec3 V = normalize(CameraPos - fragWorldPos);
    if (N.y > 0.3) N = normalize(N + (DetailNormal(fragWorldPos, time) - vec3(0,1,0)) * 0.6);

    // Find directional light
    vec3 sunDir = vec3(0, -1, 0);
    vec3 sunColor = vec3(1);
    for (uint i = 0u; i < NumActiveLights; i++) {
        if (Lights[i].Type == 0u) { sunDir = normalize(-Lights[i].Direction); sunColor = Lights[i].Color * Lights[i].Intensity; break; }
    }

    float NdotV = max(dot(N, V), 0.001);
    float fresnel = FresnelSchlick(NdotV);
    vec3 R = reflect(-V, N);
    vec3 reflectionColor = SampleSky(R, sunDir, sunColor);

    float terrainDepth = max(SEA_LEVEL - fragWorldPos.y + 8.0, 0.0);
    float depthFactor = clamp(terrainDepth / 20.0, 0.0, 1.0);
    vec3 absorption = exp(-vec3(0.45, 0.09, 0.04) * terrainDepth);
    vec3 refractionColor = mix(vec3(0.05, 0.35, 0.4), vec3(0.01, 0.05, 0.12), depthFactor) * absorption;

    float sss = pow(max(dot(V, -sunDir), 0.0), 4.0) * (1.0 - NdotV) * 0.4;
    vec3 sssColor = vec3(0.1, 0.5, 0.4) * sunColor * sss;
    vec3 causticColor = sunColor * CausticPattern(fragWorldPos, time) * 0.15 * (1.0 - depthFactor);

    float shadow = CalcShadowPCF(fragShadowCoord);
    shadow *= smoothstep(-0.1, 0.2, dot(N, sunDir));

    float NdotL = clamp(dot(N, sunDir) * 0.7 + 0.3, 0.0, 1.0);
    vec3 ambient = AmbientColor * AmbientIntensity * 0.4;
    vec3 diffuse = sunColor * NdotL * shadow;

    float NdotH = max(dot(N, normalize(V + sunDir)), 0.0);
    float spec = DistributionGGX(NdotH, 0.05) * fresnel * shadow;
    vec3 specColor = sunColor * min(spec, 50.0);

    vec3 waterBody = refractionColor * (ambient + diffuse) + causticColor * shadow + sssColor;
    vec3 finalColor = mix(waterBody, reflectionColor, fresnel) + specColor;

    float shoreDepth = clamp(terrainDepth / 3.0, 0.0, 1.0);
    float foam = 0.0;
    if (shoreDepth < 0.5) foam = (1.0 - shoreDepth * 2.0) * (sin(fragWorldPos.x*3.0+fragWorldPos.z*2.0+time*2.0)*0.5+0.5) * 0.6;
    float cf = clamp((fragWorldPos.y - SEA_LEVEL + 0.05) * 8.0, 0.0, 1.0); cf *= cf;
    foam = max(foam, cf * 0.3);
    finalColor = mix(finalColor, vec3(0.9, 0.95, 1.0) * (ambient + diffuse), foam);

    finalColor = ACESFilm(finalColor);
    if (FogEnd > 0.0) { float d = length(fragWorldPos - CameraPos); finalColor = mix(FogColor.rgb, finalColor, clamp((FogEnd-d)/(FogEnd-FogStart), 0.0, 1.0)); }

    float alpha = mix(0.6, 0.95, fresnel);
    alpha = mix(alpha, 0.85, depthFactor);
    alpha = max(alpha, foam * 0.8 + 0.2);
    outColor = vec4(finalColor, alpha);
}
