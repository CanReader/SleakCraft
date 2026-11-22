#version 450 core
#extension GL_GOOGLE_include_directive : require

// SleakCraft deferred lighting, OpenGL (Application::SetLightingShader).
// Same voxel composition as Vulkan, then the frame's output transform and
// today's display-space fog inline: OpenGL has no post chain. Engine
// materials keep the default lighting, mapped to the same output.

#include "craft/lighting_pass.glsl"

void main() {
    float depth = texture(gDepth, fragUV).r;
    if (depth >= 1.0) discard;
    gl_FragDepth = depth;

    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 rt3 = SleakReadMotionModel(px);
    SleakOutputParams o = SleakFrameOutput();
    vec3 display;
    if (CraftIsVoxelModel(SleakShadingModel(rt3))) {
        vec3 worldPos;
        vec3 scene = CraftShadeVoxel(px, fragUV, depth, rt3, worldPos);
        display = SleakApplyFog(SleakOutputTransform(scene, o), worldPos);
    } else {
        display = SleakLegacyDisplayToOutput(
            SleakShadeDefault(px, fragUV, depth), o);
    }
    display = SleakApplyDither(display, gl_FragCoord.xy, o.dither);
    outColor = SleakFrameGuard(vec4(display, 1.0));
}
