#ifndef _TIME_OF_DAY_HPP_
#define _TIME_OF_DAY_HPP_

#include <Math/Vector.hpp>
#include <Runtime/Atmosphere.hpp>

#include "VoxelLighting.hpp"
#include "WorldClock.hpp"

/// Sky and fog choices the Settings panel edits.
struct SkyOptions {
    bool fogEnabled = true;
    float fogDensity = 1.0f;  // multiplier on BSL's fog density
    bool heightFogEnabled = true;
    bool starsEnabled = true;
    float cloudCoverage = 0.75f;  // matches BSL's cloud share of the sky, 0 off
};

/// What one moment drives: voxel palette, atmosphere and engine light.
struct TimeOfDayFrame {
    LightingPalette palette;
    Sleak::AtmosphereSettings atmosphere;
    Sleak::Math::Vector3D sunRatio;      // engine sun / noon, 0 at the flip
    Sleak::Math::Vector3D ambientRatio;  // engine ambient / noon value
};

/// BSL's light and ambient keys (horizon, noon, night) blended by the world
/// clock, and the atmosphere of the same moment for a minimum-light setting,
/// a draw distance in meters and an outdoor factor (1 is open sky at the eye).
TimeOfDayFrame EvaluateTimeOfDay(const SkyState& sky, const SkyOptions& options,
                                 float minLight, float drawDistance,
                                 float outdoor);

/// BSL's underwater fog for a palette and the outdoor factor at the eye,
/// in the engine medium's linear blend.
Sleak::AtmosphereSettings::MediumParams EvaluateWaterMedium(
    const LightingPalette& palette, float outdoor);

#endif  // _TIME_OF_DAY_HPP_
