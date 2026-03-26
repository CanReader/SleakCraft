#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNorm;
layout(location = 2) in vec3 fragWorldTan;
layout(location = 3) in vec3 fragWorldBit;
layout(location = 4) in vec4 fragColor;
layout(location = 5) in vec2 fragUV;
layout(location = 6) in vec4 fragShadowCoord;
layout(location = 7) in float fragTime;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D diffuseTexture;

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

layout(set = 3, binding = 0) uniform sampler2DShadow shadowMap;

// ============================================================
// Constants
// ============================================================
const float SEA_LEVEL = 64.875;  // Water surface Y (64 + 0.875 lowered top)
const float WATER_IOR = 1.33;
const float F0_WATER = 0.02;     // Fresnel reflectance at normal incidence

// ============================================================
// Shadow (same as flat shader)
// ============================================================
float InterleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

const vec2 disk[16] = vec2[](
    vec2(-0.9465, -0.1484), vec2(-0.7431,  0.5353),
    vec2(-0.5863, -0.5879), vec2(-0.3935,  0.1025),
    vec2(-0.2428,  0.7722), vec2(-0.1074, -0.3075),
    vec2( 0.0542, -0.8645), vec2( 0.1267,  0.4300),
    vec2( 0.2787, -0.1353), vec2( 0.3842,  0.6501),
    vec2( 0.4714, -0.5537), vec2( 0.5765,  0.1675),
    vec2( 0.6712, -0.3340), vec2( 0.7527,  0.4813),
    vec2( 0.8745, -0.0910), vec2( 0.9601,  0.2637)
);

float CalcShadow(vec4 sc) {
    vec3 projCoords = sc.xyz / sc.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0)
        return 1.0;
    float biasedDepth = projCoords.z - uShadowBias;
    float angle = InterleavedGradientNoise(gl_FragCoord.xy) * 6.283185;
    float sa = sin(angle), ca = cos(angle);
    mat2 rotation = mat2(ca, sa, -sa, ca);
    float radius = uShadowTexelSize * uLightSize * 6.0;
    float shadow = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 offset = rotation * disk[i] * radius;
        shadow += texture(shadowMap, vec3(projCoords.xy + offset, biasedDepth));
    }
    shadow /= 16.0;
    return mix(1.0, shadow, uShadowStrength);
}

// ============================================================
// ACES tone mapping
// ============================================================
vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3(0.0), vec3(1.0));
}

// ============================================================
// Fresnel — full Schlick with roughness
// ============================================================
float FresnelSchlick(float cosTheta) {
    return F0_WATER + (1.0 - F0_WATER) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================
// GGX specular distribution
// ============================================================
float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}

// ============================================================
// Analytical sky reflection — procedural sky gradient
// ============================================================
vec3 SampleSky(vec3 dir) {
    // Sky gradient from horizon to zenith
    float y = max(dir.y, 0.0);
    vec3 horizonColor = vec3(0.7, 0.82, 0.95);
    vec3 zenithColor  = vec3(0.25, 0.45, 0.85);
    vec3 sky = mix(horizonColor, zenithColor, pow(y, 0.5));

    // Sun contribution in reflection
    vec3 sunDir = normalize(-uLightDir.xyz);
    float sunDot = max(dot(dir, sunDir), 0.0);
    vec3 sunColor = uLightColor.rgb * uLightColor.a;
    // Sun disk
    sky += sunColor * pow(sunDot, 512.0) * 3.0;
    // Sun glow
    sky += sunColor * pow(sunDot, 64.0) * 0.3;

    // Below horizon: dark water-ish color
    if (dir.y < 0.0) {
        float t = clamp(-dir.y * 3.0, 0.0, 1.0);
        sky = mix(horizonColor * 0.5, vec3(0.02, 0.05, 0.1), t);
    }

    return sky;
}

// ============================================================
// Volumetric absorption / scattering
// ============================================================
vec3 WaterAbsorption(float depth) {
    // Beer-Lambert absorption: different rates per channel
    // Red absorbs fastest, blue slowest — creates natural blue-green tint
    vec3 absorptionCoeff = vec3(0.45, 0.09, 0.04);
    return exp(-absorptionCoeff * max(depth, 0.0));
}

// ============================================================
// Caustics pattern (animated voronoi-like)
// ============================================================
float CausticPattern(vec3 pos, float time) {
    vec2 p = pos.xz * 0.8;
    float c = 0.0;
    // Two layers of animated caustics
    for (int i = 0; i < 2; i++) {
        float t = time * (0.8 + float(i) * 0.4);
        vec2 uv = p * (1.0 + float(i) * 0.5);
        // Simple animated caustic using sine interference
        c += sin(uv.x * 3.0 + t) * sin(uv.y * 3.0 - t * 0.7) * 0.5 + 0.5;
        c += sin(uv.x * 2.1 - t * 0.5 + uv.y * 1.7) * sin(uv.y * 2.8 + t * 0.3) * 0.5 + 0.5;
    }
    return clamp(c * 0.25, 0.0, 1.0);
}

// ============================================================
// Detail wave normal (high-frequency ripples on top of Gerstner)
// ============================================================
vec3 DetailNormal(vec3 worldPos, float time) {
    float scale1 = 2.5, scale2 = 4.0, scale3 = 7.0;
    float dx = 0.0, dz = 0.0;

    // Layer 1: medium ripples
    dx += cos(worldPos.x * scale1 + worldPos.z * 0.7 + time * 1.3) * 0.15;
    dz += cos(worldPos.z * scale1 + worldPos.x * 0.5 + time * 1.1) * 0.15;

    // Layer 2: fine ripples
    dx += cos(worldPos.x * scale2 - worldPos.z * 1.2 + time * 2.1) * 0.08;
    dz += cos(worldPos.z * scale2 - worldPos.x * 0.9 + time * 1.7) * 0.08;

    // Layer 3: micro detail
    dx += cos(worldPos.x * scale3 + worldPos.z * 2.3 + time * 3.5) * 0.04;
    dz += cos(worldPos.z * scale3 + worldPos.x * 1.8 + time * 2.9) * 0.04;

    return normalize(vec3(-dx, 1.0, -dz));
}

void main() {
    float time = fragTime;
    vec3 N = normalize(fragWorldNorm);
    vec3 V = normalize(uCameraPos.xyz - fragWorldPos);

    // Blend Gerstner macro normal with detail micro ripples
    if (N.y > 0.3) {
        vec3 detail = DetailNormal(fragWorldPos, time);
        // Blend: keep Gerstner as base, perturb with detail
        N = normalize(N + (detail - vec3(0.0, 1.0, 0.0)) * 0.6);
    }

    vec3 lightDir = normalize(uLightDir.xyz);
    vec3 sunDir = -lightDir;
    vec3 lightColor = uLightColor.rgb * uLightColor.a;

    // ---- Fresnel ----
    float NdotV = max(dot(N, V), 0.001);
    float fresnel = FresnelSchlick(NdotV);

    // ---- Reflection ----
    vec3 R = reflect(-V, N);
    vec3 reflectionColor = SampleSky(R);

    // ---- Water depth & absorption ----
    // Approximate depth from water surface Y
    float waterSurfaceY = fragWorldPos.y;
    float terrainDepth = max(SEA_LEVEL - fragWorldPos.y + 8.0, 0.0);
    vec3 absorption = WaterAbsorption(terrainDepth);

    // ---- Refraction color ----
    // Deep water base color with absorption
    vec3 shallowColor = vec3(0.05, 0.35, 0.4);
    vec3 deepColor    = vec3(0.01, 0.05, 0.12);
    float depthFactor = clamp(terrainDepth / 20.0, 0.0, 1.0);
    vec3 refractionColor = mix(shallowColor, deepColor, depthFactor) * absorption;

    // ---- Subsurface scattering ----
    // Light passing through water at shallow viewing angles
    float sssDot = max(dot(V, lightDir), 0.0);
    float sss = pow(sssDot, 4.0) * (1.0 - NdotV) * 0.4;
    vec3 sssColor = vec3(0.1, 0.5, 0.4) * lightColor * sss;

    // ---- Caustics ----
    float caustic = CausticPattern(fragWorldPos, time);
    vec3 causticColor = lightColor * caustic * 0.15 * (1.0 - depthFactor);

    // ---- Shadow ----
    float shadow = CalcShadow(fragShadowCoord);
    float rawNdotL = dot(N, sunDir);
    shadow *= smoothstep(-0.1, 0.2, rawNdotL);

    // ---- Diffuse lighting ----
    float NdotL = clamp(rawNdotL * 0.7 + 0.3, 0.0, 1.0);
    vec3 ambient = uAmbient.rgb * uAmbient.a * 0.4;
    vec3 diffuse = lightColor * NdotL * shadow;

    // ---- Specular (GGX) ----
    vec3 H = normalize(V + sunDir);
    float NdotH = max(dot(N, H), 0.0);
    float D = DistributionGGX(NdotH, 0.05); // Very smooth water
    float specular = D * fresnel * shadow;
    vec3 specColor = lightColor * min(specular, 50.0);

    // ---- Compose ----
    // Refraction: what you see through the water
    vec3 waterBody = refractionColor * (ambient + diffuse) + causticColor * shadow + sssColor;

    // Mix reflection and refraction via Fresnel
    // Fresnel determines how much you see reflection vs what's below
    vec3 finalColor = mix(waterBody, reflectionColor, fresnel);

    // Add specular on top
    finalColor += specColor;

    // ---- Foam at shoreline and wave crests ----
    float shoreDepth = clamp(terrainDepth / 3.0, 0.0, 1.0);
    float foam = 0.0;
    if (shoreDepth < 0.5) {
        // Shore foam: animated edge line
        float foamLine = sin(fragWorldPos.x * 3.0 + fragWorldPos.z * 2.0 + time * 2.0) * 0.5 + 0.5;
        foam = (1.0 - shoreDepth * 2.0) * foamLine * 0.6;
    }
    // Wave crest foam
    float crestFoam = clamp((fragWorldPos.y - SEA_LEVEL + 0.05) * 8.0, 0.0, 1.0);
    crestFoam *= crestFoam;
    foam = max(foam, crestFoam * 0.3);
    finalColor = mix(finalColor, vec3(0.9, 0.95, 1.0) * (ambient + diffuse), foam);

    // ---- ACES tone mapping ----
    finalColor = ACESFilm(finalColor);

    // ---- Distance fog ----
    if (uFogEnd > 0.0) {
        float dist = length(fragWorldPos - uCameraPos.xyz);
        float fogFactor = clamp((uFogEnd - dist) / (uFogEnd - uFogStart), 0.0, 1.0);
        finalColor = mix(uFogColor.rgb, finalColor, fogFactor);
    }

    // ---- Alpha ----
    // More opaque at glancing angles (Fresnel), less at normal incidence
    // Deep water is more opaque than shallow
    float alpha = mix(0.6, 0.95, fresnel);
    alpha = mix(alpha, 0.85, depthFactor);
    alpha = max(alpha, foam * 0.8 + 0.2);

    outColor = vec4(finalColor, alpha);
}
