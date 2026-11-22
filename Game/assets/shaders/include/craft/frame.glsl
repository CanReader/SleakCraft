#ifndef CRAFT_FRAME_GLSL
#define CRAFT_FRAME_GLSL

#include "sleak/frame.glsl"

// SleakCraft per-frame block (CraftFrameData in World/VoxelLighting.hpp),
// std140, append only; colors are linear (squared authoring values)
SLEAK_FRAME_USER_BLOCK CraftFrameUBO {
    vec4 SunColor;         // rgb direct sun or moon light
    vec4 AmbientColor;     // rgb sky light reaching shadowed faces
    vec4 MinLightColor;    // rgb floor light where the sky is hidden
    vec4 BlockLightColor;  // rgb torch tint at full block light
    vec4 NightTint;        // rgb unit hue of moonlit desaturation
    vec4 WeatherTint;      // rgb unit hue of rainy desaturation
    vec4 Sky;              // sun visibility, rain, shadow fade, night vision
    vec4 Look;             // desaturation, emission gain, albedo balance,
                           // SSAO exponent
} uCraft;

#endif
