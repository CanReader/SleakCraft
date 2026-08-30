#version 450 core

// ============================================================
// Screen Space Reflections (SSR) — OpenGL port of ssr.frag.
// View-space ray march. GL-native UV convention (UV.y=0 at bottom,
// no Y flip) — matches the GBuffer the deferred geometry pass writes.
//
// Reads (texture units):
//   0: gNormalRough  (world-space normal encoded + roughness.a)
//   1: gWorldPos     (world-space position, RGBA32F)
//   2: gDepth        (depth buffer, [0,1])
//   3: gMetalEmit    (metallic.r + emissive.gba)
//   4: gAlbedoAO     (albedo.rgb + AO.a)
//   5: sceneHDR      (lit HDR scene color)
//
// UBO (binding 11): SSRParams
//
// Output: RGBA16F premultiplied (rgb = sceneColor * weight, a = weight)
// ============================================================

in  vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D gNormalRough;
layout(binding = 1) uniform sampler2D gWorldPos;
layout(binding = 2) uniform sampler2D gDepth;
layout(binding = 3) uniform sampler2D gMetalEmit;
layout(binding = 4) uniform sampler2D gAlbedoAO;
layout(binding = 5) uniform sampler2D sceneHDR;

layout(std140, binding = 11) uniform SSRParams {
    mat4  View;
    mat4  Projection;
    vec4  CameraPos;
    vec2  ScreenSize;
    float MaxDistance;
    float Thickness;
    int   NumSteps;
    int   NumBinarySteps;
    float RoughnessThreshold;
    float _pad;
};

float InterleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// Project a view-space point to screen UV + depth.
// OpenGL: NDC.z in [-1,1] -> depth buffer [0,1], standard UV (no Y flip).
vec3 ProjectToUVDepth(vec3 viewPos) {
    vec4 clip = Projection * vec4(viewPos, 1.0);
    vec3 ndc  = clip.xyz / max(clip.w, 1e-6);
    vec3 uv;
    uv.x = ndc.x * 0.5 + 0.5;
    uv.y = ndc.y * 0.5 + 0.5;
    uv.z = ndc.z * 0.5 + 0.5;
    return uv;
}

void main() {
    float centerDepth = texture(gDepth, fragUV).r;
    if (centerDepth >= 0.9999) {
        outColor = vec4(0.0);
        return;
    }

    vec4  normalRg = texture(gNormalRough, fragUV);
    vec3  worldN   = normalize(normalRg.rgb * 2.0 - 1.0);
    float roughness = normalRg.a;

    if (roughness >= RoughnessThreshold) {
        outColor = vec4(0.0);
        return;
    }

    vec3 worldPos = texture(gWorldPos, fragUV).xyz;

    vec3 V = normalize(CameraPos.xyz - worldPos);
    vec3 R = reflect(-V, worldN);

    vec3 viewPos = (View * vec4(worldPos, 1.0)).xyz;
    vec3 viewR   = normalize(mat3(View) * R);

    float NdotV = max(dot(worldN, V), 0.0);

    float startBias = max(0.01, abs(viewPos.z) * 0.001);
    vec3  rayStartVS = viewPos + viewR * startBias;

    float totalDist = MaxDistance;
    float jitter = InterleavedGradientNoise(gl_FragCoord.xy);

    int numSteps = max(NumSteps, 1);
    float stepSize = totalDist / float(numSteps);

    float hitT            = -1.0;
    float lastSampleDepth = 0.0;
    vec3  hitUVDepth      = vec3(0.0);
    bool  skyHit          = false;

    for (int i = 1; i <= 64; ++i) {
        if (i > numSteps) break;

        float t = (float(i) + jitter - 0.5) * stepSize;
        vec3  rayVS = rayStartVS + viewR * t;

        if (rayVS.z > -0.001) continue;

        vec3 uvd = ProjectToUVDepth(rayVS);

        if (uvd.x < 0.0 || uvd.x > 1.0 || uvd.y < 0.0 || uvd.y > 1.0) continue;

        float sampleDepth = texture(gDepth, uvd.xy).r;

        if (sampleDepth >= 0.9999) {
            hitT      = t;
            hitUVDepth = uvd;
            skyHit    = true;
            break;
        }

        float delta = uvd.z - sampleDepth;
        if (delta > 0.0 && delta < Thickness) {
            hitT            = t;
            hitUVDepth      = uvd;
            lastSampleDepth = sampleDepth;
            break;
        }
    }

    if (hitT < 0.0) {
        outColor = vec4(0.0);
        return;
    }

    float lo = max(hitT - stepSize, 0.0);
    float hi = hitT;
    vec3  finalUVD = hitUVDepth;

    if (!skyHit) {
        int numBinary = max(NumBinarySteps, 0);
        for (int i = 0; i < 16; ++i) {
            if (i >= numBinary) break;

            float mid = 0.5 * (lo + hi);
            vec3  rayVS = rayStartVS + viewR * mid;
            vec3  uvd   = ProjectToUVDepth(rayVS);

            if (uvd.x < 0.0 || uvd.x > 1.0 || uvd.y < 0.0 || uvd.y > 1.0) {
                hi = mid; continue;
            }

            float sampleDepth = texture(gDepth, uvd.xy).r;
            float delta = uvd.z - sampleDepth;

            if (delta > 0.0 && delta < Thickness) {
                hi = mid;
                finalUVD = uvd;
                lastSampleDepth = sampleDepth;
            } else {
                lo = mid;
            }
        }
    }

    vec4 metalEmit = texture(gMetalEmit, fragUV);
    vec4 albedoAO  = texture(gAlbedoAO,  fragUV);
    vec3 albedo    = albedoAO.rgb;
    float metallic = metalEmit.r;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = F0 + (1.0 - F0) * pow(max(1.0 - NdotV, 0.0), 5.0);

    float roughFade = 1.0 - smoothstep(0.0, max(RoughnessThreshold, 1e-3), roughness);

    vec2 edgeDist = min(finalUVD.xy, 1.0 - finalUVD.xy);
    float edgeFade = clamp(min(edgeDist.x, edgeDist.y) * 10.0, 0.0, 1.0);

    float hitGap = skyHit ? 0.0 : max(abs(finalUVD.z - lastSampleDepth), 0.0);
    float hitConfidence = clamp(1.0 - hitGap / max(Thickness, 1e-5), 0.0, 1.0);

    float distFade = clamp(1.0 - hitT / max(MaxDistance, 1e-3), 0.0, 1.0);

    vec3 reflectedColor = texture(sceneHDR, finalUVD.xy).rgb;

    float weightScalar = roughFade * edgeFade * hitConfidence * distFade;
    vec3  weight       = fresnel * weightScalar;

    outColor = vec4(reflectedColor * weight, weightScalar);
}
