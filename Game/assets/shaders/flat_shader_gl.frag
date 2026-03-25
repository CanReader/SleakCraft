#version 450 core

in vec3 fragWorldPos;
in vec3 fragWorldNorm;
in vec4 fragColor;
in vec2 fragUV;
in vec4 fragShadowCoord;

out vec4 outColor;

struct LightData {
    vec3 Position;  uint Type;
    vec3 Direction; float Intensity;
    vec3 Color;     float Range;
    float SpotInnerCos; float SpotOuterCos;
    float AreaWidth; float AreaHeight;
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

// 16-sample Poisson Disk
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
    if (ShadowMapEnabled == 0u)
        return 1.0;

    vec3 projCoords = sc.xyz / sc.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // LightVP uses [0,1] depth range (D3D/Vulkan convention).
    // OpenGL viewport transform maps NDC Z from [-1,1] to [0,1] depth,
    // so the shadow map stores depth = (z_clip/w)*0.5 + 0.5.
    // Match by applying the same remap to our comparison reference.
    projCoords.z = projCoords.z * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    vec2 fadeCoord = smoothstep(vec2(0.0), vec2(0.05), projCoords.xy)
                   * smoothstep(vec2(0.0), vec2(0.05), vec2(1.0) - projCoords.xy);
    float edgeFade = fadeCoord.x * fadeCoord.y;

    float texelSize = ShadowTexelSize;
    float radius = 1.5;
    float shadow = 0.0;

    for (int i = 0; i < 16; i++) {
        shadow += texture(shadowMap,
            vec3(projCoords.xy + poissonDisk[i] * texelSize * radius,
                 projCoords.z - ShadowBias));
    }
    shadow /= 16.0;

    shadow = mix(1.0, shadow, ShadowStrength * edgeFade);
    return shadow;
}

// ACES film tone mapping (Stephen Hill fit)
vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3(0.0), vec3(1.0));
}

void main() {
    vec4 texColor = texture(diffuseTexture, fragUV);
    if (texColor.a < 0.5)
        discard;

    // AO is stored in vertex color. Apply only to ambient.
    float ao       = fragColor.r;
    vec3 baseColor = texColor.rgb;

    vec3 N = normalize(fragWorldNorm);

    // Hemisphere ambient
    vec3 skyAmbient    = AmbientColor * AmbientIntensity;
    vec3 groundAmbient = skyAmbient * vec3(0.55, 0.50, 0.45);
    float hemisphere   = N.y * 0.5 + 0.5;
    vec3 ambient       = mix(groundAmbient, skyAmbient, hemisphere);

    // Shadow
    float shadow = CalcShadowPCF(fragShadowCoord);

    // Wrap diffuse from first directional light
    vec3 diffuse = vec3(0.0);
    for (uint i = 0u; i < NumActiveLights; i++) {
        LightData light = Lights[i];
        if (light.Type != 0u) continue;

        vec3 L         = normalize(-light.Direction);
        float rawNdotL = dot(N, L);
        float NdotL    = clamp(rawNdotL * 0.85 + 0.15, 0.0, 1.0);
        // Fade shadow to 0 for surfaces facing away from light
        float fadedShadow = shadow * smoothstep(0.0, 0.15, rawNdotL);
        diffuse     = light.Color * light.Intensity * NdotL * fadedShadow;
        break;
    }

    // Compose: AO only on ambient
    vec3 lit = baseColor * (ao * ambient + diffuse);

    // ACES tone mapping
    lit = ACESFilm(lit);

    // Distance fog
    if (FogEnd > 0.0) {
        float dist = length(fragWorldPos - CameraPos);
        float fogFactor = clamp((FogEnd - dist) / (FogEnd - FogStart), 0.0, 1.0);
        lit = mix(FogColor.rgb, lit, fogFactor);
    }

    outColor = vec4(lit, texColor.a);
}
