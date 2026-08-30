#version 450 core

// ============================================================
// Temporal Anti-Aliasing (TAA) Resolve — OpenGL port of taa.frag.
// GL-native UV convention (UV.y=0 at bottom, no Y flip). OpenGL depth
// buffer is [0,1]; NDC z is [-1,1], so depth*2-1 when reconstructing.
//
// Reads (texture units):
//   0: currentTex  — current HDR frame
//   1: historyTex  — previous resolved frame (ping-pong)
//   2: gDepth      — depth buffer for reprojection
//
// UBO (binding 12): TAAParams
// ============================================================

in  vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D currentTex;
layout(binding = 1) uniform sampler2D historyTex;
layout(binding = 2) uniform sampler2D gDepth;

// Matrices uploaded separately (each via raw memcpy of the engine Matrix4,
// same convention SSAO uses) so we combine them here with GLSL ops — avoids
// any host-side row/column-major ambiguity. VP = Projection * View, used as
// VP * worldVec (the convention proven correct by ssao_gl / lighting_pass_gl).
layout(std140, binding = 12) uniform TAAParams {
    mat4  CurView;
    mat4  CurProj;
    mat4  PrevView;
    mat4  PrevProj;
    vec2  ScreenSize;
    float BlendFactor;   // 0.1 = 10% current; 1.0 = disable history
    float _pad;
};

void main() {
    vec2 texelSize = 1.0 / ScreenSize;

    vec3 current = texture(currentTex, fragUV).rgb;

    float depth = texture(gDepth, fragUV).r;
    if (depth >= 0.9999) {
        outColor = vec4(current, 1.0);
        return;
    }

    mat4 curVP  = CurProj  * CurView;
    mat4 prevVP = PrevProj * PrevView;

    // Reconstruct world-space position. GL-native: NDC.xyz in [-1,1].
    vec4 clipPos  = vec4(fragUV.x * 2.0 - 1.0,
                         fragUV.y * 2.0 - 1.0,
                         depth    * 2.0 - 1.0, 1.0);
    vec4 world4   = inverse(curVP) * clipPos;
    vec3 worldPos = world4.xyz / world4.w;

    // Project to previous frame.
    vec4 prevClip = prevVP * vec4(worldPos, 1.0);
    vec3 prevNDC  = prevClip.xyz / prevClip.w;

    vec2 prevUV;
    prevUV.x = prevNDC.x * 0.5 + 0.5;
    prevUV.y = prevNDC.y * 0.5 + 0.5;

    if (prevUV.x < 0.0 || prevUV.x > 1.0 ||
        prevUV.y < 0.0 || prevUV.y > 1.0) {
        outColor = vec4(current, 1.0);
        return;
    }

    // 3x3 neighborhood AABB to prevent ghosting.
    vec3 nMin = vec3(1e10);
    vec3 nMax = vec3(-1e10);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec3 s = texture(currentTex, fragUV + vec2(x, y) * texelSize).rgb;
            nMin = min(nMin, s);
            nMax = max(nMax, s);
        }
    }

    vec3 rawHistory = texture(historyTex, prevUV).rgb;
    vec3 history    = clamp(rawHistory, nMin, nMax);

    float aabbRange   = max(length(nMax - nMin), 0.001);
    float clampDist   = length(rawHistory - history);
    float clampFactor = clamp(clampDist / aabbRange * 2.0, 0.0, 1.0);
    float adaptBlend  = mix(BlendFactor, 0.5, clampFactor);

    vec3 resolved = mix(history, current, adaptBlend);
    outColor = vec4(resolved, 1.0);
}
