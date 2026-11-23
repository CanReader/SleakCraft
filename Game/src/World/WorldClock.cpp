#include "World/WorldClock.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

using Sleak::Math::Vector3D;

// Noon sun 50 degrees up, tilted south (-z), as BSL's sunPathRotation -40
constexpr double SUN_PATH_TILT_DEGREES = 40.0;
constexpr double RADIANS_PER_DEGREE = std::numbers::pi / 180.0;
constexpr double TWO_PI = 2.0 * std::numbers::pi;
// Vanilla celestial easing: c + ((1 - cos(pi c)) / 2 - c) / 3
constexpr double SUN_PATH_EASE = 1.0 / 3.0;
constexpr double HALF = 0.5;
// The sun crosses the horizon a quarter turn from noon and from midnight
constexpr double QUARTER = 0.25;
// Horizon fades of the sun and of the sky: sat(gain h + offset)
constexpr float VISIBILITY_GAIN = 10.0f;
constexpr float SKY_VISIBILITY_GAIN = 2.0f;
constexpr float VISIBILITY_OFFSET = 0.5f;
// Direct light fades between 0.02 and 0.01 path turns from the horizon
constexpr double SHADOW_FADE_END_TURNS = 0.01;
constexpr double SHADOW_FADE_RANGE_TURNS = 0.01;
// Weight of the noon keys: 1 - (1 - brightness)^1.5
constexpr double DAY_WEIGHT_EXPONENT = 1.5;
// Hour of the day at day fraction 0
constexpr double SUNRISE_HOUR = 6.0;

/// Fractional part in [0, 1).
double Fract(double v) { return v - std::floor(v); }

/// Float vector from doubles.
Vector3D ToVector(double x, double y, double z) {
    return {static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(z)};
}

}  // namespace

void WorldClock::Advance(float dt) {
    if (m_state.paused || m_frozen || !(dt > 0.0f)) return;
    m_state.dayFraction += static_cast<double>(dt) / m_state.dayLengthSeconds;
    const double whole = std::floor(m_state.dayFraction);
    m_state.day += static_cast<int64_t>(whole);
    m_state.dayFraction -= whole;
}

void WorldClock::SetDayFraction(double fraction) {
    if (std::isfinite(fraction)) m_state.dayFraction = Fract(fraction);
}

void WorldClock::SetDayLength(float seconds) {
    if (std::isfinite(seconds))
        m_state.dayLengthSeconds =
            std::clamp(seconds, MIN_DAY_LENGTH_SECONDS, MAX_DAY_LENGTH_SECONDS);
}

void WorldClock::SetState(const WorldClockState& state) {
    m_state = WorldClockState{};
    SetDayFraction(state.dayFraction);
    SetDayLength(state.dayLengthSeconds);
    m_state.day = std::max<int64_t>(state.day, 0);
    m_state.paused = state.paused;
}

SkyState WorldClock::ComputeSky() const {
    const double t = m_state.dayFraction;
    const double c = Fract(t - NOON_DAY_FRACTION);
    const double eased =
        c + ((1.0 - std::cos(std::numbers::pi * c)) * HALF - c) * SUN_PATH_EASE;
    const double theta = TWO_PI * eased;
    // Path plane: east (+x) and up tilted toward the south (-z)
    const double tilt = SUN_PATH_TILT_DEGREES * RADIANS_PER_DEGREE;
    const double pathY = std::cos(tilt);
    const double pathZ = -std::sin(tilt);
    const double sinT = std::sin(theta);
    const double cosT = std::cos(theta);

    SkyState s;
    s.sunDirection = ToVector(-sinT, cosT * pathY, cosT * pathZ);
    s.moonTangent = ToVector(cosT, sinT * pathY, sinT * pathZ);
    s.skyAxis = ToVector(0.0, -pathZ, pathY);
    s.skyAngle = static_cast<float>(theta);
    const float height = s.sunDirection.GetY();
    s.lightDirection =
        height >= 0.0f ? s.sunDirection : Vector3D() - s.sunDirection;

    const double brightness = std::max(std::sin(TWO_PI * t), 0.0);
    s.timeBrightness = static_cast<float>(brightness);
    s.dayWeight = static_cast<float>(
        1.0 - std::pow(1.0 - brightness, DAY_WEIGHT_EXPONENT));
    s.sunVisibility =
        std::clamp(VISIBILITY_GAIN * height + VISIBILITY_OFFSET, 0.0f, 1.0f);
    s.moonVisibility =
        std::clamp(-VISIBILITY_GAIN * height + VISIBILITY_OFFSET, 0.0f, 1.0f);
    s.skyVisibility = std::clamp(
        SKY_VISIBILITY_GAIN * height + VISIBILITY_OFFSET, 0.0f, 1.0f);

    // Path turns from the nearest horizon crossing
    const double crossing = std::abs(std::abs(eased - HALF) - QUARTER);
    s.shadowFade = static_cast<float>(
        std::clamp((crossing - SHADOW_FADE_END_TURNS) / SHADOW_FADE_RANGE_TURNS,
                   0.0, 1.0));
    s.moonPhase = static_cast<int>(m_state.day % MOON_PHASE_COUNT);
    return s;
}

double WorldClock::HoursToDayFraction(double hours) {
    return Fract((hours - SUNRISE_HOUR) / HOURS_PER_DAY);
}

double WorldClock::DayFractionToHours(double fraction) {
    return std::fmod(fraction * HOURS_PER_DAY + SUNRISE_HOUR, HOURS_PER_DAY);
}
