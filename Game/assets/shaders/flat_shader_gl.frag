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
    vec4 _reserved[2];
    LightData Lights[16];
};

layout(binding = 0) uniform sampler2D diffuseTexture;

void main() {
    vec4 texColor = texture(diffuseTexture, fragUV);
    vec4 baseColor = texColor * fragColor;

    vec3 N = normalize(fragWorldNorm);

    // Ambient - hemisphere
    vec3 ambientColor = AmbientColor * AmbientIntensity;
    vec3 groundColor = ambientColor * vec3(0.7, 0.65, 0.6);
    float hemisphere = N.y * 0.5 + 0.5;
    vec3 ambient = mix(groundColor, ambientColor, hemisphere);

    // Diffuse - first directional light only (matches Vulkan path)
    vec3 diffuse = vec3(0.0);
    for (uint i = 0u; i < NumActiveLights; i++) {
        LightData light = Lights[i];
        if (light.Type != 0u) continue;

        vec3 L = normalize(-light.Direction);
        float NdotL = max(dot(N, L), 0.0);
        diffuse = light.Color * light.Intensity * NdotL;
        break;
    }

    // Compose (no specular, no shadows)
    vec3 lit = baseColor.rgb * (ambient + diffuse);

    // Reinhard tone mapping
    lit = lit / (lit + vec3(1.0));

    outColor = vec4(lit, baseColor.a);
}
