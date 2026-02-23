#version 450 core

// ============================================================
// Flat Material Shader - OpenGL Fragment Shader
// Hemisphere ambient + Lambert diffuse, no specular, no shadows
// ============================================================

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

    // ---- Hemisphere ambient (sky/ground blend) ----
    vec3 ambientColor = AmbientColor * AmbientIntensity;
    vec3 groundColor = ambientColor * vec3(0.7, 0.65, 0.6);
    float hemisphere = N.y * 0.5 + 0.5;
    vec3 ambient = mix(groundColor, ambientColor, hemisphere);

    // ---- Accumulate diffuse from all lights (Lambert only) ----
    vec3 diffuse = vec3(0.0);

    for (uint i = 0u; i < NumActiveLights; i++) {
        LightData light = Lights[i];

        vec3 L;
        float attenuation = 1.0;

        if (light.Type == 0u) {
            // Directional
            L = normalize(-light.Direction);
        }
        else if (light.Type == 1u) {
            // Point
            vec3 toLight = light.Position - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            float d = dist / max(light.Range, 0.0001);
            float d2 = d * d;
            float d4 = d2 * d2;
            float falloff = clamp(1.0 - d4, 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 1.0);
        }
        else if (light.Type == 2u) {
            // Spot
            vec3 toLight = light.Position - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            float d = dist / max(light.Range, 0.0001);
            float d2 = d * d;
            float d4 = d2 * d2;
            float falloff = clamp(1.0 - d4, 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 1.0);

            float theta = dot(L, normalize(-light.Direction));
            float epsilon = light.SpotInnerCos - light.SpotOuterCos;
            float spotFactor = clamp(
                (theta - light.SpotOuterCos) / max(epsilon, 0.0001),
                0.0, 1.0);
            attenuation *= spotFactor;
        }
        else {
            // Area (fallback)
            vec3 toLight = light.Position - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            float d = dist / max(light.Range, 0.0001);
            float d2 = d * d;
            float d4 = d2 * d2;
            float falloff = clamp(1.0 - d4, 0.0, 1.0);
            attenuation = (falloff * falloff) / (dist * dist + 1.0);
        }

        float NdotL = max(dot(N, L), 0.0);
        diffuse += light.Color * light.Intensity * attenuation * NdotL;
    }

    // ---- Compose (no specular, no shadows) ----
    vec3 lit = baseColor.rgb * (ambient + diffuse);

    // ---- Tone mapping (Reinhard) ----
    lit = lit / (lit + vec3(1.0));

    outColor = vec4(lit, baseColor.a);
}
