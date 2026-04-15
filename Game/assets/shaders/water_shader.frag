#version 450

layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec4  fragColor;
layout(location = 3) in vec4  fragShadowCoord;
layout(location = 4) in float fragTime;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D diffuseTexture;

// NOTE: see water_shader.vert — no MaterialUBO in the Vulkan layout.
// Time comes from uCameraPos.w (interpolated via fragTime).
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
    vec4  uFogColor;       // horizon
    float uFogStart;
    float uFogEnd;
    vec2  _fogPad;         // std140: must be vec2 (8B), NOT float[2] (stride-16 = 32B)
    vec4  uFogZenithColor; // zenith
    float uHeightFogTop;
    float uHeightFogDensity;
    float uHeightFogFalloff;
    float uHeightFogEnabled;
};

layout(set = 3, binding = 0) uniform sampler2DShadow shadowMap;

// ============================================================
// Shadow — 4-sample PCF with IGN rotation
// ============================================================
float InterleavedGradientNoise(vec2 p) {
    vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(p, m.xy)));
}
const vec2 disk[4] = vec2[](
    vec2(-0.7431, 0.5353), vec2( 0.5765, 0.1675),
    vec2(-0.1074,-0.3075), vec2( 0.3842, 0.6501)
);
float CalcShadow(vec4 sc) {
    vec3 p = sc.xyz / sc.w;
    p.xy = p.xy * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z < 0.0 || p.z > 1.0) return 1.0;
    float angle = InterleavedGradientNoise(gl_FragCoord.xy) * 6.28318530;
    float sa = sin(angle), ca = cos(angle);
    mat2  rot = mat2(ca, sa, -sa, ca);
    float rad = uShadowTexelSize * uLightSize * 6.0;
    float s = 0.0;
    for (int i = 0; i < 4; i++) s += texture(shadowMap, vec3(p.xy + rot * disk[i] * rad, p.z - uShadowBias));
    return mix(1.0, s * 0.25, uShadowStrength);
}

// ============================================================
// Procedural wave height field (BSL-style: 2 wind layers, finite-diff normals)
// ============================================================
float WaterHeight(vec2 xz, float time) {
    vec2 w1 = vec2( time * 0.45,  time * 0.30);
    vec2 w2 = vec2(-time * 0.35,  time * 0.50);

    float h = 0.0;
    h += sin(xz.x * 0.24 + w1.x)          * sin(xz.y * 0.19 + w1.y);
    h += sin(xz.x * 0.33 + w2.x - xz.y * 0.11) * 0.65;
    h += sin(xz.x * 0.80 + w1.x * 1.9 + xz.y * 0.58) * 0.40;
    return h;
}

// Finite-difference normal — purely fragment-shader, no vertex displacement
vec3 WaveNormal(vec3 wp, float time) {
    if (fragNormal.y < 0.5) return fragNormal;   // side faces keep flat normal

    const float d = 0.15;
    float h1 = WaterHeight(wp.xz + vec2( d, 0.0), time);
    float h2 = WaterHeight(wp.xz - vec2( d, 0.0), time);
    float h3 = WaterHeight(wp.xz + vec2(0.0,  d), time);
    float h4 = WaterHeight(wp.xz - vec2(0.0,  d), time);

    float xd = (h2 - h1) / (2.0 * d);
    float zd = (h4 - h3) / (2.0 * d);
    return normalize(vec3(xd * 0.30, 1.0, zd * 0.30));
}

// ============================================================
// Sky reflection — matches game sky gradient, tinted by ambient
// ============================================================
vec3 SampleSky(vec3 dir, vec3 sunDir, vec3 sunColor, vec3 amb) {
    float y = max(dir.y, 0.0);
    vec3 horiz  = mix(vec3(0.70, 0.82, 0.95), amb, 0.30);
    vec3 zenith = mix(vec3(0.22, 0.42, 0.82), amb * 0.65, 0.25);
    vec3 sky = mix(horiz, zenith, sqrt(y));
    float sd = max(dot(dir, sunDir), 0.0);
    float sd2 = sd * sd;
    sky += sunColor * pow(sd2 * sd2 * sd, 12.0) * 0.5;
    if (dir.y < 0.0) sky = mix(horiz * 0.5, vec3(0.02, 0.04, 0.08), clamp(-dir.y * 4.0, 0.0, 1.0));
    return sky;
}

// ============================================================
// Helpers
// ============================================================
vec3  ACESFilm(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), vec3(0), vec3(1)); }
float FresnelSchlick(float c) { return 0.02 + 0.98 * pow(clamp(1.0 - c, 0.0, 1.0), 5.0); }
float GGX(float NdotH, float r) { float a=r*r*r*r; float d=NdotH*NdotH*(a-1.0)+1.0; return a/(3.14159265*d*d); }

// Sky-matched gradient fog + exponential height fog. Mirrors the ApplyFog
// helper in lighting_pass.frag so water blends seamlessly into the fogged sky.
vec3 ApplyFog(vec3 color, vec3 worldPos) {
    if (uFogEnd <= 0.0) return color;
    vec3 toFrag = worldPos - uCameraPos.xyz;
    float dist = length(toFrag.xz);
    float distFog = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
    float heightFog = 0.0;
    if (uHeightFogEnabled > 0.5) {
        float h = max(uHeightFogTop - worldPos.y, 0.0);
        heightFog = clamp(uHeightFogDensity * (1.0 - exp(-h * uHeightFogFalloff)),
                          0.0, 1.0);
    }
    float fogAmount = 1.0 - (1.0 - distFog) * (1.0 - heightFog);
    vec3 viewDir = normalize(toFrag);
    float t = smoothstep(0.0, 1.0, clamp(viewDir.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 fogColor = mix(uFogColor.rgb, uFogZenithColor.rgb, t);
    return mix(color, fogColor, fogAmount);
}

// ============================================================
// Main
// ============================================================
void main() {
    float time   = fragTime;
    vec3  N      = WaveNormal(fragWorldPos, time);
    vec3  V      = normalize(uCameraPos.xyz - fragWorldPos);
    vec3  sunDir = normalize(-uLightDir.xyz);
    vec3  sunCol = uLightColor.rgb * uLightColor.a;
    vec3  amb    = uAmbient.rgb * uAmbient.a;

    float NdotV  = max(dot(N, V), 0.001);
    float fresnel = FresnelSchlick(NdotV);

    // ---- Shadow & diffuse ----
    float shadow = CalcShadow(fragShadowCoord);
    shadow *= smoothstep(-0.1, 0.2, dot(N, sunDir));
    float NdotL  = clamp(dot(N, sunDir) * 0.65 + 0.35, 0.0, 1.0);
    vec3  diffuse = sunCol * NdotL * shadow;

    // ---- BSL water color: R=64 G=160 B=255 I=0.35, then squared ----
    // waterColorSqrt = vec3(64,160,255)/255 * 0.35  →  squared  →  *3 to lift brightness
    vec3 wcSqrt  = vec3(64.0, 160.0, 255.0) / 255.0 * 0.35;
    vec3 wColor  = wcSqrt * wcSqrt * 3.0;          // ≈ (0.023, 0.145, 0.368)
    vec3 waterBody = wColor * (amb * 1.8 + diffuse * 0.9);

    // Subsurface forward scatter (light passing through shallow water)
    float sss = pow(max(dot(V, -sunDir), 0.0), 4.0) * (1.0 - NdotV) * 0.35;
    waterBody += vec3(0.0, 0.08, 0.14) * sunCol * sss;

    // ---- Sky reflection ----
    vec3 R    = reflect(-V, N);
    vec3 refl = SampleSky(R, sunDir, sunCol, amb);

    // ---- GGX specular ----
    vec3  H     = normalize(V + sunDir);
    float NdotH = max(dot(N, H), 0.0);
    float spec  = GGX(NdotH, 0.07) * fresnel * shadow;
    vec3  specC = sunCol * min(spec, 40.0);

    // ---- Compose: water body at normal incidence, sky at grazing ----
    vec3 finalColor = mix(waterBody, refl, fresnel) + specC;

    // ---- Caustic shimmer (subtle, projected down from wave normals) ----
    if (fragNormal.y > 0.5) {
        vec2  cp = fragWorldPos.xz * 0.65;
        float t  = time * 0.9;
        float c  = sin(cp.x*2.7+t) * sin(cp.y*2.7-t*0.75) * 0.5 + 0.5;
        finalColor += sunCol * c * c * 0.05 * shadow;
    }

    // ---- ACES + fog ----
    finalColor = ACESFilm(finalColor);
    finalColor = ApplyFog(finalColor, fragWorldPos);

    // ---- Alpha: BSL waterAlpha=0.70 base, Fresnel boosts edges to ~0.95 ----
    float alpha = mix(0.72, 0.97, pow(clamp(1.0 - NdotV, 0.0, 1.0), 2.0));

    outColor = vec4(finalColor, alpha);
}
