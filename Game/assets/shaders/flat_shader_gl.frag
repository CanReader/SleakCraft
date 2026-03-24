#version 450 core

in vec3 fragWorldPos;
in vec3 fragWorldNorm;
in vec4 fragColor;
in vec2 fragUV;

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

layout(binding = 0) uniform sampler2D diffuseTexture;

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

    // Wrap diffuse from first directional light
    vec3 diffuse = vec3(0.0);
    for (uint i = 0u; i < NumActiveLights; i++) {
        LightData light = Lights[i];
        if (light.Type != 0u) continue;

        vec3 L      = normalize(-light.Direction);
        float NdotL = clamp(dot(N, L) * 0.85 + 0.15, 0.0, 1.0);
        diffuse     = light.Color * light.Intensity * NdotL;
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
