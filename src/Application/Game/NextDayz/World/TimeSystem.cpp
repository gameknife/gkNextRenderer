#include "TimeSystem.hpp"

#include <algorithm>
#include <cmath>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace NextDayz
{
    namespace
    {
        float SmoothStep(float a, float b, float x)
        {
            const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    void TimeSystem::Reset(const FTimeConfig& config)
    {
        config_ = config;
        gameMinutes_ = config.StartHour * 60.0;
        timeScale_ = config.TimeScale;
        overcast_ = false;
        daylight_ = 1.0f;
    }

    double TimeSystem::Hour() const
    {
        return std::fmod(gameMinutes_, 24.0 * 60.0) / 60.0;
    }

    int TimeSystem::HourInt() const
    {
        return static_cast<int>(Hour());
    }

    int TimeSystem::MinuteInt() const
    {
        const double dayMinutes = std::fmod(gameMinutes_, 24.0 * 60.0);
        return static_cast<int>(std::fmod(dayMinutes, 60.0));
    }

    void TimeSystem::Tick(double deltaRealSeconds, Assets::Scene& scene)
    {
        gameMinutes_ += deltaRealSeconds * timeScale_;
        ApplyEnvironment(scene);
    }

    void TimeSystem::ApplyEnvironment(Assets::Scene& scene)
    {
        const float hour = static_cast<float>(Hour());

        // Dawn 05:00-07:00 rise, dusk 17:00-19:00 fall.
        const float rise = SmoothStep(5.0f, 7.0f, hour);
        const float set = 1.0f - SmoothStep(17.0f, 19.0f, hour);
        daylight_ = std::min(rise, set);

        const float weather = overcast_ ? config_.OvercastFactor : 1.0f;

        auto& env = scene.GetEnvSettings();
        // 06:00 -> 18:00 sweeps half a turn (east rise, west set).
        env.SunRotation = std::clamp((hour - 6.0f) / 12.0f, 0.0f, 1.0f);
        env.HasSun = daylight_ > 0.02f && !overcast_;
        env.SunIntensity = config_.DaySunIntensity * daylight_ * weather;
        env.SkyIntensity = config_.DaySkyIntensity *
                           (config_.NightSkyFraction + (1.0f - config_.NightSkyFraction) * daylight_) * weather;
    }
}
