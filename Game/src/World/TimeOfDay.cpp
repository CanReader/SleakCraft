#include "World/TimeOfDay.hpp"

#include <algorithm>
#include <cmath>

#include "World/WorldGenerator.hpp"

namespace {

using Sleak::Math::Vector3D;

// Horizon (sunrise and sunset), night and sky keys, BSL v10 defaults with
// the horizon intensities matched to the BSL sunset shots
constexpr AuthoredKey LIGHT_HORIZON_KEY{255.0f, 160.0f, 80.0f, 1.02f};
constexpr AuthoredKey LIGHT_NIGHT_KEY{96.0f, 192.0f, 255.0f, 1.00f};
constexpr AuthoredKey AMBIENT_HORIZON_KEY{255.0f, 204.0f, 144.0f, 0.56f};
constexpr AuthoredKey AMBIENT_NIGHT_KEY{96.0f, 192.0f, 255.0f, 0.60f};
constexpr AuthoredKey SKY_KEY{96.0f, 160.0f, 255.0f, 1.00f};
// Night keys x 0.3, less an eighth per moon phase away from full
constexpr float NIGHT_KEY_SCALE = 0.3f;
constexpr float MOON_PHASE_DIM = 0.125f;
static_assert(1.0f - MOON_PHASE_DIM * (MOON_PHASE_COUNT / 2) == 0.5f,
              "a new moon keeps half the night light");
// Authoring values squared are linear light
constexpr float LINEAR_POWER = 2.0f;
// Sky exposure 2^(gain b + offset); night 2^-3.5
constexpr float DAY_EXPOSURE_GAIN = 0.75f;
constexpr float DAY_EXPOSURE_OFFSET = -0.75f;
constexpr float NIGHT_SKY_EXPOSURE_LOG2 = -3.5f;
// Sun-side tint: light^(4 - v), share (1 - 0.6 b)^3, exponent 1 + 24 (1 - v)
constexpr float TINT_POWER_BASE = 4.0f;
constexpr float TINT_DIM = 0.6f;
constexpr float TINT_DIM_POWER = 3.0f;
constexpr float TINT_EXPONENT_NOON = 1.0f;
constexpr float TINT_EXPONENT_TWILIGHT_GAIN = 24.0f;
// Ground darkening: 0.1 (4 - 3 v) max(fog density, 0.125)
constexpr float GROUND_BASE = 0.1f;
constexpr float GROUND_SKY_BASE = 4.0f;
constexpr float GROUND_SKY_GAIN = 3.0f;
constexpr float FOG_DENSITY_FLOOR = 0.125f;
// Discs: 4 x intensity^2 (BSL's sun and moon intensity 1.5)
constexpr float DISC_INTENSITY = 1.5f;
constexpr float DISC_GAIN = 4.0f * DISC_INTENSITY * DISC_INTENSITY;
// Sun disc: mix(horizon, sqrt(noon horizon 1.4), b), reddened by 4 v
constexpr float SUN_DISC_NOON_GAIN = 1.40f;
constexpr float SUN_REDDEN_GAIN = 4.0f;
// Moon: night^1.6 x the vanilla moon's luma
constexpr float MOON_TINT_POWER = 1.6f;
constexpr float MOON_SURFACE_LUMA = 0.5f;
// Stars: full-moon night^0.8 x 5 x the brightest hash excess 0.1875
constexpr float STAR_TINT_POWER = 0.8f;
constexpr float STAR_GAIN = 5.0f * 0.1875f;
// Glare: 0.25 light, lobe width 0.1 x the falloff of the day part
constexpr float GLARE_GAIN = 0.25f;
constexpr float GLARE_WIDTH_SCALE = 0.1f;
constexpr float GLARE_HORIZON_WIDTH = 0.25f;
constexpr float GLARE_NOON_WIDTH = 0.10f;
constexpr float GLARE_NIGHT_WIDTH = 0.50f;
// Fog: density per 1024 m, x4 at night; exponent 1.25 + 0.35 by clear day
constexpr float FOG_METRES_PER_UNIT = 1024.0f;
constexpr float FOG_NIGHT_DENSITY = 4.0f;
constexpr float FOG_EXPONENT_BASE = 1.25f;
constexpr float FOG_EXPONENT_CLEAR = 0.35f;
// Height fog starts thinning 2 blocks below sea level (BSL's y 62)
constexpr int FOG_BASE_BELOW_SEA_LEVEL = 2;
// Enclosed fog color: the minimum light x 0.5
constexpr float FOG_INDOOR_LIGHT = 0.5f;
// The edge fade covers the last fifth of the draw distance
constexpr float BORDER_START_FRACTION = 0.8f;
// Clouds: vanilla cloud height, BSL's drift of about 4 m/s toward -x
constexpr float CLOUD_BASE_HEIGHT = 192.0f;
constexpr float CLOUD_WIND_X = -4.0f;
constexpr float CLOUD_WIND_Z = 0.0f;
// Cloud brightness 0.5 by day, 0.25 at night, times the day exposure
constexpr float CLOUD_BRIGHTNESS_DAY = 0.5f;
constexpr float CLOUD_BRIGHTNESS_NIGHT_DROP = 0.25f;
// Underwater fog (BSL): water albedo x 1.44 x (0.9 light + 0.1), 35 % at
// zero distance and 1 - exp(-2 d / 64), blended in square-root space
constexpr AuthoredKey WATER_KEY{64.0f, 160.0f, 255.0f, 0.35f};
constexpr float WATER_FOG_GAIN = 1.44f;
constexpr float WATER_FOG_LIGHT = 0.9f;
constexpr float WATER_FOG_FLOOR = 0.1f;
constexpr float WATER_FOG_OPACITY = 0.35f;
constexpr float WATER_FOG_DENSITY = 2.0f / 64.0f;
// A bright scene keeps (1 - o)^2 of itself under a square-root blend
constexpr float SQRT_BLEND_POWER = 2.0f;

/// Authoring color scaled by a factor.
SqrtColor Scaled(const SqrtColor& c, float factor) {
    return {c.r * factor, c.g * factor, c.b * factor};
}

/// Each channel of an authoring color raised to a power.
Vector3D Power(const SqrtColor& c, float exponent) {
    return {std::pow(c.r, exponent), std::pow(c.g, exponent),
            std::pow(c.b, exponent)};
}

/// Linear light of a color over that of its noon key, per channel.
Vector3D NoonRatio(const SqrtColor& c, const SqrtColor& noon) {
    return Power({c.r / noon.r, c.g / noon.g, c.b / noon.b}, LINEAR_POWER);
}

/// Linear blend of two scalars.
float Mix(float from, float to, float t) { return from + (to - from) * t; }

}  // namespace

TimeOfDayFrame EvaluateTimeOfDay(const SkyState& sky, const SkyOptions& options,
                                 float minLight, float drawDistance,
                                 float outdoor) {
    const float tb = sky.timeBrightness;
    const float sv = sky.skyVisibility;
    const int fromFull =
        std::min(sky.moonPhase, MOON_PHASE_COUNT - sky.moonPhase);
    const float night = NIGHT_KEY_SCALE * (1.0f - MOON_PHASE_DIM * fromFull);
    const SqrtColor lightNight = Scaled(ToSqrtColor(LIGHT_NIGHT_KEY), night);
    const SqrtColor lightHorizon = ToSqrtColor(LIGHT_HORIZON_KEY);
    const SqrtColor lightNoon = ToSqrtColor(NOON_SUN_KEY);
    const SqrtColor ambientNoon = ToSqrtColor(NOON_AMBIENT_KEY);
    const SqrtColor lightSun =
        BlendColor(lightHorizon, lightNoon, sky.dayWeight);
    const SqrtColor ambientSun = BlendColor(ToSqrtColor(AMBIENT_HORIZON_KEY),
                                            ambientNoon, sky.dayWeight);
    const SqrtColor light = BlendColor(lightNight, lightSun, sky.sunVisibility);
    const SqrtColor ambient =
        BlendColor(Scaled(ToSqrtColor(AMBIENT_NIGHT_KEY), night), ambientSun,
                   sky.sunVisibility);

    TimeOfDayFrame f;
    // Voxel palette; moon, weather and torch keys stay the noon palette's
    f.palette = VoxelLighting::NoonPalette();
    f.palette.sun = light;
    f.palette.ambient = ambient;
    f.palette.sunVisibility = sky.sunVisibility;
    f.palette.shadowFade = sky.shadowFade;
    // Engine light (engine materials): noon-relative, fades at the flip
    f.sunRatio = NoonRatio(light, lightNoon) * sky.shadowFade;
    f.ambientRatio = NoonRatio(ambient, ambientNoon);

    Sleak::AtmosphereSettings& a = f.atmosphere;
    a.sky.color = Power(ToSqrtColor(SKY_KEY), LINEAR_POWER);
    const float exposure =
        std::exp2(DAY_EXPOSURE_GAIN * tb + DAY_EXPOSURE_OFFSET);
    a.sky.dayScale = exposure * sv;
    a.sky.tintColor = Power(lightSun, TINT_POWER_BASE - sv);
    a.sky.tintStrength = std::pow(1.0f - TINT_DIM * tb, TINT_DIM_POWER);
    a.sky.tintExponent =
        TINT_EXPONENT_NOON + TINT_EXPONENT_TWILIGHT_GAIN * (1.0f - sv);
    a.sky.nightColor =
        Power(lightNight, LINEAR_POWER) * std::exp2(NIGHT_SKY_EXPOSURE_LOG2);
    const float daySky = std::max(sky.sunVisibility, sv);
    a.sky.dayWeight = daySky * daySky;
    a.sky.groundDensity = GROUND_BASE *
                          (GROUND_SKY_BASE - GROUND_SKY_GAIN * sv) *
                          std::max(options.fogDensity, FOG_DENSITY_FLOOR);

    const auto disc = [tb](float horizon, float noon) {
        return Mix(horizon, std::sqrt(noon * horizon * SUN_DISC_NOON_GAIN), tb);
    };
    a.sun.direction = sky.sunDirection;
    a.sun.color = Vector3D(disc(lightHorizon.r, lightNoon.r),
                           disc(lightHorizon.g, lightNoon.g),
                           disc(lightHorizon.b, lightNoon.b));
    a.sun.intensity = DISC_GAIN * sky.sunVisibility;
    a.sun.reddenGain = SUN_REDDEN_GAIN * sky.sunVisibility;

    a.moon.direction = Vector3D() - sky.sunDirection;
    a.moon.tangent = sky.moonTangent;
    a.moon.color = Power(lightNight, MOON_TINT_POWER) *
                   (DISC_GAIN * MOON_SURFACE_LUMA * sky.moonVisibility);
    a.moon.phase = static_cast<float>(sky.moonPhase) / MOON_PHASE_COUNT;

    const SqrtColor fullMoonNight =
        Scaled(ToSqrtColor(LIGHT_NIGHT_KEY), NIGHT_KEY_SCALE);
    a.stars.color = options.starsEnabled
                        ? Power(fullMoonNight, STAR_TINT_POWER) *
                              (STAR_GAIN * sky.moonVisibility)
                        : Vector3D();
    a.stars.axis = sky.skyAxis;
    a.stars.angle = sky.skyAngle;

    a.glare.direction = sky.lightDirection;
    a.glare.width =
        GLARE_WIDTH_SCALE * Mix(GLARE_NIGHT_WIDTH,
                                Mix(GLARE_HORIZON_WIDTH, GLARE_NOON_WIDTH, tb),
                                sky.sunVisibility);
    a.glare.color = Power(light, LINEAR_POWER) * (GLARE_GAIN * sky.shadowFade);

    // Fog thickens toward night under open sky, not indoors
    const float density =
        Mix(1.0f, outdoor / Mix(1.0f / FOG_NIGHT_DENSITY, 1.0f, sv), outdoor);
    a.fog.heightEnabled = options.heightFogEnabled;
    a.fog.densityPerMeter =
        options.fogEnabled ? options.fogDensity * density / FOG_METRES_PER_UNIT
                           : 0.0f;
    a.fog.exponent = FOG_EXPONENT_BASE + FOG_EXPONENT_CLEAR * sv * outdoor;
    a.fog.baseHeight = static_cast<float>(WorldGenerator::SEA_LEVEL -
                                          FOG_BASE_BELOW_SEA_LEVEL);
    a.fog.dayWeight = sv * sv;
    const float indoor =
        VoxelLighting::MinLightLinear(minLight) * FOG_INDOOR_LIGHT;
    a.fog.indoorColor = Vector3D(indoor, indoor, indoor);
    a.fog.outdoor = outdoor;

    a.border.start = BORDER_START_FRACTION * drawDistance;
    a.border.end = drawDistance;

    // Clouds: direct light only (sky light lifts the bases above BSL's grey)
    const float cloudScale =
        (CLOUD_BRIGHTNESS_DAY - CLOUD_BRIGHTNESS_NIGHT_DROP * (1.0f - sv)) *
        exposure;
    a.clouds.coverage = options.cloudCoverage;
    a.clouds.baseHeight = CLOUD_BASE_HEIGHT;
    a.clouds.wind = Sleak::Math::Vector2D(CLOUD_WIND_X, CLOUD_WIND_Z);
    a.clouds.lightColor = Power(light, LINEAR_POWER) * cloudScale;
    a.clouds.lightFade = sky.shadowFade;
    return f;
}

Sleak::AtmosphereSettings::MediumParams EvaluateWaterMedium(
    const LightingPalette& palette, float outdoor) {
    // Albedo (tint x gain^2)^2 of craft/water.glsl; the key holds one gain
    const Vector3D water =
        Power(Scaled(ToSqrtColor(WATER_KEY), WATER_KEY.intensity),
              LINEAR_POWER) *
        WATER_FOG_GAIN;
    const float lit = WATER_FOG_LIGHT * outdoor * palette.shadowFade;
    const Vector3D unlit(WATER_FOG_FLOOR, WATER_FOG_FLOOR, WATER_FOG_FLOOR);
    Sleak::AtmosphereSettings::MediumParams m;
    m.color = water * (Power(palette.sun, LINEAR_POWER) * lit + unlit);
    m.veil = 1.0f - std::pow(1.0f - WATER_FOG_OPACITY, SQRT_BLEND_POWER);
    m.densityPerMeter = WATER_FOG_DENSITY * SQRT_BLEND_POWER;
    return m;
}
