#version 450 core

// ============================================================
// Composite Pass (OpenGL) — ALWAYS RUNS, final output to the default FBO.
//
// Baseline-preservation design: in the OpenGL deferred path the lighting
// pass, skybox and forward objects already write their FINAL tonemapped
// (ACES) LDR color into the HDR scene target. To keep the effects-off image
// pixel-identical to the historical direct-to-FBO0 path, this composite is a
// PASSTHROUGH of the scene color and only:
//   - adds bloom (when bloomStrength > 0)
//   - mixes SSR reflections (when SSR enabled)
// It does NOT re-apply ACES/fog/gamma (the scene is already display-ready).
//
// Note vs Vulkan: Vulkan keeps the scene in linear HDR and applies ACES here.
// OpenGL's forward shaders historically pre-tonemap, so a second ACES would
// double-tonemap and darken the image. Bloom therefore thresholds against
// LDR scene values (see RenderBloomGL threshold tuning) rather than HDR.
//
// Reads (texture units):
//   0: sceneHDR  (scene color — already ACES LDR in the OpenGL path)
//   1: bloomTex  (bloom pyramid mip 0)
//   2: ssrTex    (premultiplied SSR; rgb = color*weight, a = weight)
// ============================================================

in  vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneHDR;
layout(binding = 1) uniform sampler2D bloomTex;
layout(binding = 2) uniform sampler2D ssrTex;

uniform float uBloomStrength;
uniform int   uSSREnabled;

void main() {
    vec3 color = texture(sceneHDR, fragUV).rgb;

    // SSR: premultiplied. ssr.rgb = reflectedColor*Fresnel (pre-scaled by
    // fades), ssr.a = coverage. Unpremultiply and lerp over the scene so
    // reflections replace (rather than wash out) the surface specular.
    if (uSSREnabled != 0) {
        vec4 ssr = texture(ssrTex, fragUV);
        if (ssr.a > 0.001) {
            vec3 reflColor = ssr.rgb / max(ssr.a, 1e-4);
            color = mix(color, reflColor, clamp(ssr.a, 0.0, 1.0));
        }
    }

    // Bloom is additive — only brightens hot spots, never dims the base.
    vec3 bloom = texture(bloomTex, fragUV).rgb;
    color += bloom * clamp(uBloomStrength, 0.0, 1.0);

    outColor = vec4(color, 1.0);
}
