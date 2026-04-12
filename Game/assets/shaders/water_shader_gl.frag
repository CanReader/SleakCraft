#version 450 core

in vec3  fragWorldPos;
in vec3  fragNormal;
in vec4  fragColor;
in vec4  fragShadowCoord;
in float fragTime;

out vec4 outColor;

struct LightData {
    vec3 Position; uint Type;
    vec3 Direction; float Intensity;
    vec3 Color;    float Range;
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
    vec3  CameraPos;
    uint  NumActiveLights;
    vec3  AmbientColor;
    float AmbientIntensity;
    vec4  FogColor;
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

// ============================================================
// Shadow — 16-sample PCF with IGN rotation (identical to Vulkan)
// ============================================================
float InterleavedGradientNoise(vec2 p) {
    vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(p, m.xy)));
}
const vec2 disk[16] = vec2[](
    vec2(-0.9465,-0.1484), vec2(-0.7431, 0.5353), vec2(-0.5863,-0.5879), vec2(-0.3935, 0.1025),
    vec2(-0.2428, 0.7722), vec2(-0.1074,-0.3075), vec2( 0.0542,-0.8645), vec2( 0.1267, 0.4300),
    vec2( 0.2787,-0.1353), vec2( 0.3842, 0.6501), vec2( 0.4714,-0.5537), vec2( 0.5765, 0.1675),
    vec2( 0.6712,-0.3340), vec2( 0.7527, 0.4813), vec2( 0.8745,-0.0910), vec2( 0.9601, 0.2637)
);
float CalcShadow(vec4 sc) {
    if (ShadowMapEnabled == 0u) return 1.0;
    vec3 p = sc.xyz / sc.w;
    p.xy = p.xy * 0.5 + 0.5;
    p.z  = p.z  * 0.5 + 0.5;   // GL depth [-1,1] -> [0,1]
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    vec2 fc = smoothstep(vec2(0.0), vec2(0.05), p.xy) * smoothstep(vec2(0.0), vec2(0.05), vec2(1.0) - p.xy);
    float angle = InterleavedGradientNoise(gl_FragCoord.xy) * 6.28318530;
    float sa = sin(angle), ca = cos(angle);
    mat2  rot = mat2(ca, sa, -sa, ca);
    float rad = ShadowTexelSize * ShadowLightSize * 6.0;
    float s = 0.0;
    for (int i = 0; i < 16; i++) s += texture(shadowMap, vec3(p.xy + rot * disk[i] * rad, p.z - ShadowBias));
    return mix(1.0, s / 16.0, ShadowStrength * fc.x * fc.y);
}

// ============================================================
// Procedural wave height field
// ============================================================
float WaterHeight(vec2 xz, float time) {
    vec2 w1 = vec2( time * 0.45,  time * 0.30);
    vec2 w2 = vec2(-time * 0.35,  time * 0.50);
    float h = 0.0;
    h += sin(xz.x * 0.24 + w1.x)               * sin(xz.y * 0.19 + w1.y);
    h += sin(xz.x * 0.33 + w2.x - xz.y * 0.11) * 0.65;
    h += sin(xz.x * 0.80 + w1.x * 1.9 + xz.y * 0.58) * 0.40;
    h += sin(xz.x * 1.10 - w2.x * 2.1) * sin(xz.y * 0.88 + w2.y * 1.4) * 0.35;
    h += sin(xz.x * 2.20 + w1.x * 3.3) * sin(xz.y * 1.85 - w1.y * 2.7) * 0.15;
    h += sin(xz.x * 3.00 - w2.x * 4.2 + xz.y * 2.30) * 0.10;
    return h;
}
vec3 WaveNormal(vec3 wp, float time) {
    if (fragNormal.y < 0.5) return fragNormal;
    const float d = 0.15;
    float xd = (WaterHeight(wp.xz - vec2(d,0), time) - WaterHeight(wp.xz + vec2(d,0), time)) / (2.0*d);
    float zd = (WaterHeight(wp.xz - vec2(0,d), time) - WaterHeight(wp.xz + vec2(0,d), time)) / (2.0*d);
    return normalize(vec3(xd * 0.30, 1.0, zd * 0.30));
}

// ============================================================
// Sky reflection (identical to Vulkan)
// ============================================================
vec3 SampleSky(vec3 dir, vec3 sunDir, vec3 sunColor, vec3 amb) {
    float y = max(dir.y, 0.0);
    vec3 horiz  = mix(vec3(0.70, 0.82, 0.95), amb, 0.30);
    vec3 zenith = mix(vec3(0.22, 0.42, 0.82), amb * 0.65, 0.25);
    vec3 sky = mix(horiz, zenith, pow(y, 0.45));
    float sd = max(dot(dir, sunDir), 0.0);
    sky += sunColor * pow(sd, 500.0) * 5.0 + sunColor * pow(sd, 60.0) * 0.5;
    if (dir.y < 0.0) sky = mix(horiz * 0.5, vec3(0.02, 0.04, 0.08), clamp(-dir.y * 4.0, 0.0, 1.0));
    return sky;
}

vec3  ACESFilm(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), vec3(0), vec3(1)); }
float FresnelSchlick(float c) { return 0.02 + 0.98 * pow(clamp(1.0-c,0.0,1.0), 5.0); }
float GGX(float NdotH, float r) { float a=r*r*r*r; float d=NdotH*NdotH*(a-1.0)+1.0; return a/(3.14159265*d*d); }

void main() {
    float time = fragTime;
    vec3  N    = WaveNormal(fragWorldPos, time);
    vec3  V    = normalize(CameraPos - fragWorldPos);

    // Find directional light
    vec3 sunDir = vec3(0.0, 1.0, 0.0);
    vec3 sunCol = vec3(1.0);
    for (uint i = 0u; i < NumActiveLights; i++) {
        if (Lights[i].Type == 0u) {
            sunDir = normalize(-Lights[i].Direction);
            sunCol = Lights[i].Color * Lights[i].Intensity;
            break;
        }
    }
    vec3 amb = AmbientColor * AmbientIntensity;

    float NdotV  = max(dot(N, V), 0.001);
    float fresnel = FresnelSchlick(NdotV);

    float shadow = CalcShadow(fragShadowCoord);
    shadow *= smoothstep(-0.1, 0.2, dot(N, sunDir));
    float NdotL  = clamp(dot(N, sunDir) * 0.65 + 0.35, 0.0, 1.0);
    vec3  diffuse = sunCol * NdotL * shadow;

    // BSL water color
    vec3 wcSqrt  = vec3(64.0, 160.0, 255.0) / 255.0 * 0.35;
    vec3 wColor  = wcSqrt * wcSqrt * 3.0;
    vec3 waterBody = wColor * (amb * 1.8 + diffuse * 0.9);

    float sss = pow(max(dot(V, -sunDir), 0.0), 4.0) * (1.0 - NdotV) * 0.35;
    waterBody += vec3(0.0, 0.08, 0.14) * sunCol * sss;

    vec3  R    = reflect(-V, N);
    vec3  refl = SampleSky(R, sunDir, sunCol, amb);

    vec3  H     = normalize(V + sunDir);
    float NdotH = max(dot(N, H), 0.0);
    float spec  = GGX(NdotH, 0.07) * fresnel * shadow;
    vec3  specC = sunCol * min(spec, 40.0);

    vec3 finalColor = mix(waterBody, refl, fresnel) + specC;

    // Caustic shimmer
    if (fragNormal.y > 0.5) {
        vec2  cp = fragWorldPos.xz * 0.65;
        float t  = time * 0.9;
        float c  = sin(cp.x*2.7+t)*sin(cp.y*2.7-t*0.75)*0.5+0.5;
        c += sin(cp.x*1.9-t*0.55+cp.y*1.6)*sin(cp.y*2.4+t*0.35)*0.5+0.5;
        c  = clamp(pow(c * 0.5, 1.6) * 1.5, 0.0, 1.0);
        finalColor += sunCol * c * 0.05 * shadow;
    }

    finalColor = ACESFilm(finalColor);
    if (FogEnd > 0.0) {
        float dist = length(fragWorldPos - CameraPos);
        finalColor = mix(FogColor.rgb, finalColor, clamp((FogEnd-dist)/(FogEnd-FogStart), 0.0, 1.0));
    }

    float alpha = mix(0.72, 0.97, pow(clamp(1.0-NdotV, 0.0, 1.0), 2.0));
    outColor = vec4(finalColor, alpha);
}
