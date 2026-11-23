#version 450 core
#extension GL_GOOGLE_include_directive : require

// SleakCraft deferred lighting, OpenGL (Application::SetLightingShader).
// Same voxel composition as Vulkan, then the atmosphere (sky and fog in
// scene units, as the Vulkan composite applies them) and the frame's output
// transform inline: OpenGL has no post chain. Engine materials keep the
// default lighting, mapped to the same output.

#include "craft/lighting_pass.glsl"
#include "sleak/atmosphere_pass.glsl"

/// Display color of a scene-unit color seen through the atmosphere.
vec3 CraftThroughAtmosphere(vec3 scene, vec2 uv, float depth,
                            SleakOutputParams o) {
    vec4 scatter = SleakAtmosphereGuard(SleakAtmosphereComposite(uv, depth));
    return SleakOutputTransform(SleakAtmosphereApply(scene, scatter), o);
}

void main() {
    float depth = texture(gDepth, fragUV).r;
    bool atmosphere = SleakAtmosphereEnabled();
    if (depth >= 1.0 && !atmosphere) discard;
    gl_FragDepth = depth;

    SleakOutputParams o = SleakFrameOutput();
    vec3 display;
    if (depth >= 1.0) {
        display = SleakAtmosphereSkyDisplay(fragUV);
    } else {
        ivec2 px = ivec2(gl_FragCoord.xy);
        vec4 rt3 = SleakReadMotionModel(px);
        if (CraftIsVoxelModel(SleakShadingModel(rt3))) {
            vec3 worldPos;
            vec3 scene = CraftShadeVoxel(px, fragUV, depth, rt3, worldPos);
            display = atmosphere
                          ? CraftThroughAtmosphere(scene, fragUV, depth, o)
                          : SleakApplyFog(SleakOutputTransform(scene, o),
                                          worldPos);
        } else {
            display = SleakLegacyDisplayToOutput(
                SleakShadeDefault(px, fragUV, depth), o);
            if (atmosphere)
                display = CraftThroughAtmosphere(
                    SleakOutputTransformInverse(display, o), fragUV, depth,
                    o);
        }
    }
    display = SleakApplyDither(display, gl_FragCoord.xy, o.dither);
    outColor = SleakFrameGuard(vec4(display, 1.0));
}
