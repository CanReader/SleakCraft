#include "World/VoxelLighting.hpp"

#include <Core/Application.hpp>
#include <Core/GraphicsConfig.hpp>
#include <Core/Logger.hpp>
#include <algorithm>
#include <cmath>

namespace {

// Authoring channels run 0-255
constexpr float CHANNEL_MAX = 255.0f;
// Moon, rain and torch keys (BSL v10 defaults)
constexpr AuthoredKey MOON_KEY{96.0f, 192.0f, 255.0f, 1.00f};
constexpr AuthoredKey RAIN_KEY{176.0f, 224.0f, 255.0f, 1.20f};
constexpr AuthoredKey TORCH_KEY{255.0f, 212.0f, 160.0f, 0.85f};
// Cave floor: grey authored like the palette, then scaled down
constexpr float MIN_LIGHT_GREY = 128.0f;
constexpr float MIN_LIGHT_SCALE = 0.04f;
// Desaturation keeps 1 - this * factor of the color's chroma
constexpr float DESAT_PER_FACTOR = 0.4f;
// Emissive gain, albedo balancing, SSAO exponent (BSL defaults)
constexpr float EMISSION_GAIN = 4.0f;
constexpr float ALBEDO_BALANCE = 0.25f;
constexpr float SSAO_EXPONENT = 1.0f;
// Keeps a tint hue defined when its color is black
constexpr float TINT_EPSILON = 1e-6f;
constexpr int RGB = 3;

/// Linear light of an authored color, written as rgb plus a zero w.
void WriteLinear(const SqrtColor& c, float out[4]) {
    out[0] = c.r * c.r;
    out[1] = c.g * c.g;
    out[2] = c.b * c.b;
    out[3] = 0.0f;
}

/// Linear light of an authored color times a settings multiplier.
void WriteScaled(const SqrtColor& c, const LightMultiplier& m, float out[4]) {
    WriteLinear(c, out);
    const float gain[RGB] = {m.r, m.g, m.b};
    for (int i = 0; i < RGB; ++i)
        out[i] *= std::max(gain[i], 0.0f) * std::max(m.scale, 0.0f);
}

/// Unit-length hue of an authored color's linear light.
void WriteHue(const SqrtColor& c, float out[4]) {
    WriteLinear(c, out);
    const float len =
        std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    for (int i = 0; i < RGB; ++i) out[i] /= std::max(len, TINT_EPSILON);
}

}  // namespace

SqrtColor ToSqrtColor(const AuthoredKey& key) {
    const float scale = key.intensity / CHANNEL_MAX;
    return {key.r * scale, key.g * scale, key.b * scale};
}

SqrtColor BlendColor(const SqrtColor& from, const SqrtColor& to, float t) {
    return {from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t,
            from.b + (to.b - from.b) * t};
}

LightingPalette VoxelLighting::NoonPalette() {
    LightingPalette p;
    p.sun = ToSqrtColor(NOON_SUN_KEY);
    p.ambient = ToSqrtColor(NOON_AMBIENT_KEY);
    p.moon = ToSqrtColor(MOON_KEY);
    p.weather = ToSqrtColor(RAIN_KEY);
    p.blockLight = ToSqrtColor(TORCH_KEY);
    return p;
}

void VoxelLighting::Upload(const Sleak::GraphicsConfig& config) const {
    CraftFrameData d{};
    WriteScaled(m_palette.sun, m_sunMultiplier, d.SunColor);
    WriteScaled(m_palette.ambient, m_ambientMultiplier, d.AmbientColor);
    const SqrtColor grey = ToSqrtColor(
        {MIN_LIGHT_GREY, MIN_LIGHT_GREY, MIN_LIGHT_GREY, config.minLight});
    WriteLinear(grey, d.MinLightColor);
    for (int i = 0; i < RGB; ++i) d.MinLightColor[i] *= MIN_LIGHT_SCALE;
    WriteLinear(m_palette.blockLight, d.BlockLightColor);
    WriteHue(m_palette.moon, d.NightTint);
    WriteHue(m_palette.weather, d.WeatherTint);

    d.Sky[0] = m_palette.sunVisibility;
    d.Sky[1] = m_palette.rain;
    d.Sky[2] = m_palette.shadowFade;
    d.Sky[3] = m_palette.nightVision;

    d.Look[0] =
        config.desaturationEnabled
            ? std::max(1.0f - DESAT_PER_FACTOR * config.desaturationFactor,
                       0.0f)
            : 0.0f;
    d.Look[1] = EMISSION_GAIN;
    d.Look[2] = ALBEDO_BALANCE;
    d.Look[3] = SSAO_EXPONENT;

    auto* app = Sleak::Application::GetInstance();
    if (app && !app->SetFrameUserData(&d, sizeof(d)))
        SLEAK_ERROR("VoxelLighting: frame block of {} bytes rejected",
                    sizeof(d));
}
