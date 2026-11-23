#ifndef _WORLD_CLOCK_HPP_
#define _WORLD_CLOCK_HPP_

#include <Math/Vector.hpp>
#include <cstdint>

/// Moon phases in a lunar cycle; one per day.
inline constexpr int MOON_PHASE_COUNT = 8;
/// Hours on the clock face; day fraction 0 is 06:00.
inline constexpr float HOURS_PER_DAY = 24.0f;
/// Day fraction of noon; worlds saved without a clock load there.
inline constexpr double NOON_DAY_FRACTION = 0.25;
/// Seconds per day: a Minecraft day by default, and the accepted range.
inline constexpr float DEFAULT_DAY_LENGTH_SECONDS = 1200.0f;
inline constexpr float MIN_DAY_LENGTH_SECONDS = 60.0f;
inline constexpr float MAX_DAY_LENGTH_SECONDS = 7200.0f;

/// Clock state saved in world.dat (version 2); a new world starts at sunrise.
struct WorldClockState {
    double dayFraction = 0.0;  // 0 is 06:00, 0.25 noon
    int64_t day = 0;           // whole days elapsed
    float dayLengthSeconds = DEFAULT_DAY_LENGTH_SECONDS;
    bool paused = false;
};

/// Sun, moon and light geometry and BSL's time scalars for one moment.
struct SkyState {
    Sleak::Math::Vector3D sunDirection;    // toward the sun, moon opposite
    Sleak::Math::Vector3D lightDirection;  // sun while up, else moon
    Sleak::Math::Vector3D moonTangent;     // moon's direction of travel
    Sleak::Math::Vector3D skyAxis;         // axis the sky turns around
    float skyAngle = 0.0f;                 // radians turned since noon
    float timeBrightness = 0.0f;           // max(sin 2 pi t, 0)
    float dayWeight = 0.0f;                // weight of the noon keys
    float sunVisibility = 0.0f;
    float moonVisibility = 0.0f;
    float skyVisibility = 0.0f;
    float shadowFade = 0.0f;
    int moonPhase = 0;  // 0 full, below MOON_PHASE_COUNT
};

/// Day and night of one world: advances with game time, is saved in
/// world.dat and places the sun on BSL's path tilted 40 degrees.
/// @ingroup world
class WorldClock {
   public:
    /// Advances by dt seconds unless paused or frozen.
    void Advance(float dt);
    /// Jumps to a day fraction (0 is 06:00, 0.25 noon).
    void SetDayFraction(double fraction);
    /// Current day fraction in [0, 1).
    double GetDayFraction() const { return m_state.dayFraction; }
    /// Stops or resumes the clock; saved with the world.
    void SetPaused(bool paused) { m_state.paused = paused; }
    /// True while the saved pause is on.
    bool IsPaused() const { return m_state.paused; }
    /// Stops the clock for this session only (--freeze-time).
    void SetFrozenForSession(bool frozen) { m_frozen = frozen; }
    /// Seconds per day, clamped to the accepted range.
    void SetDayLength(float seconds);
    /// Seconds per day.
    float GetDayLength() const { return m_state.dayLengthSeconds; }
    /// State to save in world.dat.
    const WorldClockState& GetState() const { return m_state; }
    /// Restores a loaded state, repairing out-of-range fields.
    void SetState(const WorldClockState& state);
    /// Geometry and time scalars of the current moment.
    SkyState ComputeSky() const;

    /// Day fraction of an hour of the day (06:00 is 0).
    static double HoursToDayFraction(double hours);
    /// Hour of the day of a day fraction.
    static double DayFractionToHours(double fraction);

   private:
    WorldClockState m_state;
    bool m_frozen = false;
};

#endif  // _WORLD_CLOCK_HPP_
