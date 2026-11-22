#ifndef _VOXEL_LIGHTING_HPP_
#define _VOXEL_LIGHTING_HPP_

#include <cstddef>

namespace Sleak {
struct GraphicsConfig;
}

/// A palette color in authoring space: (r, g, b) / 255 times an intensity.
/// Palettes blend here; shaders get the square, which is linear light.
struct SqrtColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

/// A palette key as authored: 0-255 channels and an intensity.
struct AuthoredKey {
    float r;
    float g;
    float b;
    float intensity;
};

/// Noon sun and sky ambient keys (BSL v10 defaults).
inline constexpr AuthoredKey NOON_SUN_KEY{196.0f, 220.0f, 255.0f, 1.40f};
inline constexpr AuthoredKey NOON_AMBIENT_KEY{120.0f, 172.0f, 255.0f, 0.60f};

/// SqrtColor of an authored key.
SqrtColor ToSqrtColor(const AuthoredKey& key);

/// Linear blend in authoring space, as time-of-day keys mix.
SqrtColor BlendColor(const SqrtColor& from, const SqrtColor& to, float t);

/// Light colors and sky state of one moment, in authoring space.
struct LightingPalette {
    SqrtColor sun;
    SqrtColor ambient;
    SqrtColor moon;
    SqrtColor weather;
    SqrtColor blockLight;
    float sunVisibility = 1.0f;
    float rain = 0.0f;
    float shadowFade = 1.0f;
    float nightVision = 0.0f;
};

/// Linear multiplier the settings panel applies on top of a palette color.
struct LightMultiplier {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float scale = 1.0f;
};

/// SleakCraft's per-frame shader block (craft/frame.glsl), std140, append
/// only. Colors are linear.
struct CraftFrameData {
    float SunColor[4];
    float AmbientColor[4];
    float MinLightColor[4];
    float BlockLightColor[4];
    float NightTint[4];
    float WeatherTint[4];
    float Sky[4];
    float Look[4];
};
static_assert(offsetof(CraftFrameData, SunColor) == 0);
static_assert(offsetof(CraftFrameData, AmbientColor) == 16);
static_assert(offsetof(CraftFrameData, MinLightColor) == 32);
static_assert(offsetof(CraftFrameData, BlockLightColor) == 48);
static_assert(offsetof(CraftFrameData, NightTint) == 64);
static_assert(offsetof(CraftFrameData, WeatherTint) == 80);
static_assert(offsetof(CraftFrameData, Sky) == 96);
static_assert(offsetof(CraftFrameData, Look) == 112);
static_assert(sizeof(CraftFrameData) == 128);

/// Owns the voxel lighting palette and sends it as the game frame block.
class VoxelLighting {
   public:
    /// BSL-style noon: blue-white sun, blue sky ambient, open sky.
    static LightingPalette NoonPalette();

    /// Palette used from the next Upload on.
    void SetPalette(const LightingPalette& palette) { m_palette = palette; }
    /// Palette of the next Upload.
    const LightingPalette& GetPalette() const { return m_palette; }

    /// Settings multipliers on the sun and sky ambient colors.
    void SetMultipliers(const LightMultiplier& sun,
                        const LightMultiplier& ambient) {
        m_sunMultiplier = sun;
        m_ambientMultiplier = ambient;
    }

    /// Builds the block from the palette and the look settings and sends
    /// it; called every frame, since scene activation clears the block.
    void Upload(const Sleak::GraphicsConfig& config) const;

   private:
    LightingPalette m_palette = NoonPalette();
    LightMultiplier m_sunMultiplier;
    LightMultiplier m_ambientMultiplier;
};

#endif  // _VOXEL_LIGHTING_HPP_
